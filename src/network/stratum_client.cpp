#include "ohmy/stratum_client.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/hex_utils.hpp"
#include "ohmy/ethash.hpp" // for epochFromSeedHash
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <cinttypes>  // for PRIx64
#include <cmath>       // for std::round

using json = nlohmann::json;

namespace ohmy {
namespace network {

class StratumClient::Impl {
public:
    explicit Impl(const std::string& poolUrl, const std::string& walletAddress)
        : poolUrl_(poolUrl)
        , walletAddress_(walletAddress)
        , connected_(false)
        , socket_(-1)
        , messageId_(1)
        , currentDifficulty_(0xFFFFFFFFFFFFFFFF)
    {}

    ~Impl() {
        disconnect();
    }

    bool connect() {
        // Parse pool URL (format: host:port or stratum+tcp://host:port)
        std::string host;
        uint16_t port = 3333;  // Default Ethereum mining port
        
        size_t colonPos = poolUrl_.find_last_of(':');
        if (colonPos != std::string::npos) {
            host = poolUrl_.substr(0, colonPos);
            
            // Remove protocol prefix if present
            size_t protocolEnd = host.find("://");
            if (protocolEnd != std::string::npos) {
                host = host.substr(protocolEnd + 3);
            }
            
            port = std::stoi(poolUrl_.substr(colonPos + 1));
        } else {
            host = poolUrl_;
        }
        
        LOG_INFO("Connecting to pool: " + host + ":" + std::to_string(port));
        
        // Resolve hostname
        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr) {
            LOG_ERROR("Failed to resolve host: " + host);
            return false;
        }
        
        // Create socket
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            LOG_ERROR("Failed to create socket");
            return false;
        }
        
        // Setup server address
        struct sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        std::memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        // Connect
        if (::connect(socket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            LOG_ERROR("Failed to connect to pool");
            close(socket_);
            socket_ = -1;
            return false;
        }
        
        connected_ = true;
        LOG_INFO("Connected to pool successfully");
        return true;
    }

    void disconnect() {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
            connected_ = false;
            LOG_INFO("Disconnected from pool");
        }
    }

    bool isConnected() const {
        return connected_;
    }

    bool subscribe() {
        if (!connected_) return false;
        
        // Construct mining.subscribe message
        json message = {
            {"id", messageId_++},
            {"method", "mining.subscribe"},
            {"params", {"ohmy-miner-etc/1.0", nullptr}}
        };
        
        std::string msg = message.dump() + "\n";
        LOG_INFO("Subscribing to pool: " + msg);
        
        if (!sendMessage(msg)) {
            return false;
        }
        
        // Read response
        std::string response = receiveMessage();
        if (response.empty()) {
            LOG_ERROR("Failed to receive subscribe response");
            return false;
        }
        
        LOG_INFO("Subscribe response: " + response);
        
        try {
            json resp = json::parse(response);
            if (resp.contains("result") && !resp["result"].is_null()) {
                // Extract subscription details
                if (resp["result"].is_array() && resp["result"].size() >= 2) {
                    subscriptionId_ = resp["result"][1].get<std::string>();
                    LOG_INFO("Subscription ID: " + subscriptionId_);
                }
                return true;
            }
        } catch (const json::exception& e) {
            LOG_ERROR("Failed to parse subscribe response: " + std::string(e.what()));
        }
        
        return false;
    }

    bool authorize(const std::string& workerName) {
        if (!connected_) return false;
        
        std::string worker = walletAddress_;
        if (!workerName.empty()) {
            worker += "." + workerName;
        }
        currentWorker_ = worker; // remember for submissions
        
        // Construct mining.authorize message
        json message = {
            {"id", messageId_++},
            {"method", "mining.authorize"},
            {"params", {worker, ""}}  // worker, password (usually empty)
        };
        
        std::string msg = message.dump() + "\n";
        LOG_INFO("Authorizing worker: " + worker);
        
        if (!sendMessage(msg)) {
            return false;
        }
        
        // Read response
        std::string response = receiveMessage();
        if (response.empty()) {
            LOG_ERROR("Failed to receive authorize response");
            return false;
        }
        
        LOG_INFO("Authorize response: " + response);
        
        try {
            json resp = json::parse(response);
            if (resp.contains("result") && resp["result"].get<bool>()) {
                LOG_INFO("Authorization successful!");
                // Optionally, suggest a lower difficulty if env var is set (for faster share rate while debugging)
                const char* suggestEnv = std::getenv("OHMY_SUGGEST_DIFF");
                if (suggestEnv) {
                    uint64_t sug = std::strtoull(suggestEnv, nullptr, 10);
                    if (sug > 0) {
                        json suggest = {
                            {"id", messageId_++},
                            {"method", "mining.suggest_difficulty"},
                            {"params", {sug}}
                        };
                        std::string smsg = suggest.dump() + "\n";
                        LOG_INFO("Suggesting difficulty to pool: " + std::to_string(sug));
                        sendMessage(smsg);
                        // We don't expect a response; pool may ignore or later issue mining.set_difficulty
                    }
                }
                return true;
            } else if (resp.contains("method") && resp["method"].get<std::string>() == "mining.notify") {
                // Some pools send mining.notify immediately after authorization instead of a result
                LOG_INFO("Authorization response contains mining.notify - treating as successful");
                // Process this notification
                processNotification(resp);
                
                // Suggest difficulty if requested
                const char* suggestEnv = std::getenv("OHMY_SUGGEST_DIFF");
                if (suggestEnv) {
                    uint64_t sug = std::strtoull(suggestEnv, nullptr, 10);
                    if (sug > 0) {
                        json suggest = {
                            {"id", messageId_++},
                            {"method", "mining.suggest_difficulty"},
                            {"params", {sug}}
                        };
                        std::string smsg = suggest.dump() + "\n";
                        LOG_INFO("Suggesting difficulty to pool: " + std::to_string(sug));
                        sendMessage(smsg);
                    }
                }
                return true;
            } else {
                LOG_ERROR("Authorization failed");
            }
        } catch (const json::exception& e) {
            LOG_ERROR("Failed to parse authorize response: " + std::string(e.what()));
        }
        
        return false;
    }

    bool submitSolution(const Solution& solution) {
        if (!connected_) return false;
        
        // Format nonce as 16-char hex string (no 0x prefix)
        char nonceBuf[17];
        std::snprintf(nonceBuf, sizeof(nonceBuf), "%016" PRIx64, solution.nonce);
        
        // Use jobId from solution (to avoid stale shares)
        std::string jobId = solution.jobId.empty() ? currentJob_.jobId : solution.jobId;
        
        // Convert mixHash to hex string
        std::string mixHashHex = utils::HexUtils::hash32ToHex(solution.mixHash, true); // with 0x prefix
        
        // Get header from current job
        std::string headerHex = utils::HexUtils::hash32ToHex(currentJob_.headerHash, true); // with 0x prefix
        
        // Log solution details for debugging
        LOG_DEBUG("Submitting: nonce=0x" + std::string(nonceBuf) + 
                  " mixHash=" + mixHashHex.substr(0, 18) + "... job=" + jobId);
        
        // Ethash Stratum expects 5 params: [worker, jobId, nonce, headerHash, mixHash]
        json message = {
            {"id", messageId_++},
            {"method", "mining.submit"},
            {"params", {
                currentWorker_.empty() ? walletAddress_ : currentWorker_,
                jobId,
                std::string(nonceBuf),  // 16 hex chars, no 0x
                headerHex,              // header hash with 0x prefix  
                mixHashHex              // mix hash with 0x prefix
            }}
        };
        
        std::string msg = message.dump() + "\n";
        LOG_INFO("Submitting solution: " + msg);
        
        if (!sendMessage(msg)) {
            return false;
        }
        
        // Read response
        std::string response = receiveMessage();
        if (!response.empty()) {
            LOG_INFO("Submit response: " + response);
            
            try {
                json resp = json::parse(response);
                if (resp.contains("result") && resp["result"].is_boolean() && resp["result"].get<bool>()) {
                    LOG_INFO("✓ Share accepted!");
                    return true;
                } else {
                    if (resp.contains("error") && !resp["error"].is_null()) {
                        try {
                            auto err = resp["error"];
                            int code = -1; std::string msg;
                            if (err.is_array() && err.size() >= 2) {
                                code = err[0].get<int>();
                                msg = err[1].get<std::string>();
                            } else if (err.is_object()) {
                                code = err.value("code", -1);
                                msg = err.value("message", std::string(""));
                            }
                            LOG_ERROR("✗ Share rejected (" + std::to_string(code) + "): " + msg);
                        } catch (...) {
                            LOG_ERROR("✗ Share rejected (unparsed error)");
                        }
                    } else {
                        LOG_ERROR("✗ Share rejected");
                    }
                }
            } catch (const json::exception& e) {
                LOG_ERROR("Failed to parse submit response: " + std::string(e.what()));
            }
        }
        
        return false;
    }
    
    void setOnJob(OnJobCallback callback) {
        onJob_ = callback;
    }
    
    void setOnDifficulty(OnDifficultyCallback callback) {
        onDifficulty_ = callback;
    }
    
    MiningJob getCurrentJob() const {
        return currentJob_;
    }
    
    bool processMessages() {
        if (!connected_ || socket_ < 0) return false;
        
        // Set socket to non-blocking mode
        int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
        
        char buffer[4096];
        ssize_t received = ::recv(socket_, buffer, sizeof(buffer) - 1, 0);
        
        // Restore blocking mode
        fcntl(socket_, F_SETFL, flags);
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available - not an error
                return false;
            }
            LOG_ERROR("Failed to receive message");
            return false;
        }
        
        if (received == 0) {
            LOG_INFO("Connection closed by pool");
            disconnect();
            return false;
        }
        
        buffer[received] = '\0';
        std::string message(buffer, received);
        
        // Parse JSON messages - can be multiple per line or split across lines
        // Strategy: Try to parse JSON objects one by one from the buffer
        bool processed = false;
        size_t pos = 0;
        
        while (pos < message.length()) {
            // Skip whitespace and newlines
            while (pos < message.length() && (message[pos] == ' ' || message[pos] == '\n' || message[pos] == '\r')) {
                pos++;
            }
            
            if (pos >= message.length()) break;
            
            // Find the start of a JSON object
            if (message[pos] != '{') {
                pos++;
                continue;
            }
            
            // Find the matching closing brace
            int braceCount = 0;
            size_t jsonStart = pos;
            size_t jsonEnd = pos;
            bool inString = false;
            bool escaped = false;
            
            while (jsonEnd < message.length()) {
                char c = message[jsonEnd];
                
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = !inString;
                } else if (!inString) {
                    if (c == '{') {
                        braceCount++;
                    } else if (c == '}') {
                        braceCount--;
                        if (braceCount == 0) {
                            jsonEnd++;
                            break;
                        }
                    }
                }
                jsonEnd++;
            }
            
            if (braceCount != 0) {
                // Incomplete JSON, wait for more data
                break;
            }
            
            // Extract and parse the JSON object
            std::string jsonStr = message.substr(jsonStart, jsonEnd - jsonStart);
            
            try {
                json msg = json::parse(jsonStr);
                
                LOG_DEBUG("Parsed JSON with " + std::to_string(jsonStr.length()) + " bytes");
                LOG_DEBUG("Full JSON: " + jsonStr);
                
                // Check if it's a notification (has "method" field)
                if (msg.contains("method")) {
                    LOG_DEBUG("Found method field, processing notification...");
                    processNotification(msg);
                    processed = true;
                } else if (msg.contains("result") || msg.contains("error")) {
                    // This is a response to our previous request
                    LOG_DEBUG("Found result/error field, response: " + jsonStr.substr(0, 200));
                }
            } catch (const json::exception& e) {
                LOG_ERROR("Failed to parse message: " + std::string(e.what()));
                LOG_ERROR("Message was: " + jsonStr.substr(0, 100));
            }
            
            pos = jsonEnd;
        }
        
        return processed;
    }

private:
    bool sendMessage(const std::string& message) {
        if (socket_ < 0) return false;
        
        ssize_t sent = ::send(socket_, message.c_str(), message.length(), 0);
        if (sent < 0) {
            LOG_ERROR("Failed to send message");
            return false;
        }
        
        return true;
    }
    
    std::string receiveMessage() {
        if (socket_ < 0) return "";
        
        char buffer[4096];
        ssize_t received = ::recv(socket_, buffer, sizeof(buffer) - 1, 0);
        
        if (received < 0) {
            LOG_ERROR("Failed to receive message");
            return "";
        }
        
        if (received == 0) {
            LOG_INFO("Connection closed by pool");
            disconnect();
            return "";
        }
        
        buffer[received] = '\0';
        std::string msg(buffer, received);
        if (!msg.empty()) {
            LOG_INFO("← Received: " + msg.substr(0, 200) + (msg.length() > 200 ? "..." : ""));
        }
        return msg;
    }
    
    void processNotification(const json& message) {
        try {
            std::string method = message["method"].get<std::string>();
            LOG_DEBUG("Processing notification: " + method);
            
            if (method == "mining.notify") {
                // Parse mining.notify message
                // For Ethash pools (like 2miners), format is:
                // Params: [jobId, seedHash, headerHash, cleanJobs]
                const auto& params = message["params"];
                
                LOG_DEBUG("mining.notify params size: " + std::to_string(params.size()));
                for (size_t i = 0; i < params.size(); i++) {
                    LOG_DEBUG("  param[" + std::to_string(i) + "]: " + params[i].dump().substr(0, 60));
                }
                
                if (params.size() >= 3) {
                    MiningJob job;
                    job.jobId = params[0].get<std::string>();
                    
                    LOG_DEBUG("Job ID: " + job.jobId);
                    
                    // Parse hex strings - Ethash format
                    std::string seedHashHex = params[1].get<std::string>();
                    std::string headerHashHex = params[2].get<std::string>();
                    
                    LOG_DEBUG("Seed: " + seedHashHex);
                    LOG_DEBUG("Header: " + headerHashHex.substr(0, 20) + "...");
                    
                    // Convert hex strings using HexUtils
                    if (!utils::HexUtils::hexToHash32(headerHashHex, job.headerHash)) {
                        LOG_WARN("Failed to parse header hash: " + headerHashHex);
                    }
                    
                    if (!utils::HexUtils::hexToHash32(seedHashHex, job.seedHash)) {
                        LOG_WARN("Failed to parse seed hash: " + seedHashHex);
                    }
                    
                    // Derive epoch from seed hash if possible
                    uint32_t derivedEpoch = Ethash::epochFromSeedHash(job.seedHash);
                    if (derivedEpoch == UINT32_MAX) {
                        LOG_WARN("Failed to derive epoch from seed hash; defaulting to 0");
                        derivedEpoch = 0;
                    }
                    job.epoch = derivedEpoch;
                    job.blockNumber = derivedEpoch * 30000; // approximate lower bound
                    // Use current difficulty as target (will be converted externally to 256-bit boundary)
                    job.target = currentDifficulty_;
                    
                    // Clean jobs flag
                    bool cleanJobs = params.size() > 3 ? params[3].get<bool>() : true;
                    
                    currentJob_ = job;
                    
                    LOG_INFO("✓ New mining job received: " + job.jobId);
                    
                    if (onJob_) {
                        LOG_DEBUG("Calling onJob callback...");
                        onJob_(job);
                    } else {
                        LOG_WARN("onJob callback is not set!");
                    }
                } else {
                    LOG_WARN("mining.notify has too few params: " + std::to_string(params.size()));
                }
            } else if (method == "mining.set_difficulty") {
                // Parse difficulty change
                const auto& params = message["params"];
                LOG_DEBUG("mining.set_difficulty received with " + std::to_string(params.size()) + " params");
                if (params.size() >= 1) {
                    LOG_DEBUG("Raw param[0]: " + params[0].dump());
                    
                    // Difficulty can come as integer or floating point
                    uint64_t difficulty = 0;
                    if (params[0].is_number_integer()) {
                        difficulty = params[0].get<uint64_t>();
                    } else if (params[0].is_number_float()) {
                        double diffDouble = params[0].get<double>();
                        difficulty = static_cast<uint64_t>(std::round(diffDouble));
                    } else {
                        LOG_WARN("Unexpected difficulty type: " + std::string(params[0].type_name()));
                        difficulty = 1; // fallback
                    }
                    
                    currentDifficulty_ = difficulty;
                    // Convert difficulty -> 64-bit target approximation: max64/difficulty
                    // This is a simplification; proper Ethash uses 256-bit boundary.
                    uint64_t target64 = difficulty > 0 ? (std::numeric_limits<uint64_t>::max() / difficulty) : std::numeric_limits<uint64_t>::max();
                    currentJob_.target = target64;
                    LOG_INFO("✓ Difficulty set to: " + std::to_string(difficulty));
                    LOG_INFO("   Target64 (approximate): " + std::to_string(target64));
                    
                    if (onDifficulty_) {
                        LOG_DEBUG("Calling onDifficulty callback with diff=" + std::to_string(difficulty));
                        onDifficulty_(difficulty);
                    } else {
                        LOG_WARN("   onDifficulty callback is not set!");
                    }
                }
            }
        } catch (const json::exception& e) {
            LOG_ERROR("Failed to process notification: " + std::string(e.what()));
        }
    }

    std::string poolUrl_;
    std::string walletAddress_;
    bool connected_;
    int socket_;
    uint64_t messageId_;
    uint64_t currentDifficulty_;
    std::string subscriptionId_;
    MiningJob currentJob_;
    OnJobCallback onJob_;
    OnDifficultyCallback onDifficulty_;
    std::string currentWorker_;
};

// StratumClient implementation
StratumClient::StratumClient(const std::string& poolUrl, const std::string& walletAddress)
    : pImpl_(std::make_unique<Impl>(poolUrl, walletAddress))
{}

StratumClient::~StratumClient() = default;

bool StratumClient::connect() {
    return pImpl_->connect();
}

void StratumClient::disconnect() {
    pImpl_->disconnect();
}

bool StratumClient::isConnected() const {
    return pImpl_->isConnected();
}

bool StratumClient::subscribe() {
    return pImpl_->subscribe();
}

bool StratumClient::authorize(const std::string& workerName) {
    return pImpl_->authorize(workerName);
}

bool StratumClient::submitSolution(const Solution& solution) {
    return pImpl_->submitSolution(solution);
}

bool StratumClient::processMessages() {
    return pImpl_->processMessages();
}

void StratumClient::setOnJob(OnJobCallback callback) {
    pImpl_->setOnJob(callback);
}

void StratumClient::setOnDifficulty(OnDifficultyCallback callback) {
    pImpl_->setOnDifficulty(callback);
}

MiningJob StratumClient::getCurrentJob() const {
    return pImpl_->getCurrentJob();
}

} // namespace network
} // namespace ohmy
