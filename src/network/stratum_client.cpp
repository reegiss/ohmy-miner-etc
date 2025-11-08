#include "ohmy/stratum_client.hpp"
#include "ohmy/logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

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
        
        // Convert solution to hex strings
        std::stringstream nonceHex;
        nonceHex << "0x" << std::hex << solution.nonce;
        
        // Construct mining.submit message
        json message = {
            {"id", messageId_++},
            {"method", "mining.submit"},
            {"params", {
                walletAddress_,           // worker name
                currentJob_.jobId,        // job id
                nonceHex.str(),           // nonce
                currentJob_.headerHash,   // header hash
                "0x0"                     // mix hash (TODO: calculate actual)
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
                if (resp.contains("result") && resp["result"].get<bool>()) {
                    LOG_INFO("✓ Share accepted!");
                    return true;
                } else {
                    LOG_ERROR("✗ Share rejected");
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
        
        // Parse and process JSON message
        try {
            json msg = json::parse(message);
            
            // Check if it's a notification (has "method" field)
            if (msg.contains("method")) {
                processNotification(msg);
                return true;
            }
        } catch (const json::exception& e) {
            LOG_ERROR("Failed to parse message: " + std::string(e.what()));
        }
        
        return false;
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
        return std::string(buffer, received);
    }
    
    void processNotification(const json& message) {
        try {
            std::string method = message["method"].get<std::string>();
            
            if (method == "mining.notify") {
                // Parse mining.notify message
                // Params: [jobId, prevHash, coinbase1, coinbase2, merkleBranch, version, nBits, nTime, cleanJobs]
                const auto& params = message["params"];
                
                if (params.size() >= 8) {
                    MiningJob job;
                    job.jobId = params[0].get<std::string>();
                    
                    // Parse hex strings to hash32_t
                    std::string headerHashHex = params[1].get<std::string>();
                    std::string seedHashHex = params[2].is_string() ? params[2].get<std::string>() : "";
                    std::string targetHex = params[6].get<std::string>();
                    
                    // Convert hex strings (remove 0x prefix if present)
                    if (headerHashHex.substr(0, 2) == "0x") headerHashHex = headerHashHex.substr(2);
                    if (seedHashHex.substr(0, 2) == "0x") seedHashHex = seedHashHex.substr(2);
                    if (targetHex.substr(0, 2) == "0x") targetHex = targetHex.substr(2);
                    
                    // For now, just store jobId - actual hex conversion would need helper function
                    // TODO: Implement hex string to hash32_t conversion
                    job.target = std::stoull(targetHex.substr(0, 16), nullptr, 16);
                    job.blockNumber = 0;  // Will be set from pool if provided
                    job.epoch = 0;
                    
                    // Clean jobs flag
                    bool cleanJobs = params.size() > 8 ? params[8].get<bool>() : false;
                    
                    currentJob_ = job;
                    
                    LOG_INFO("New mining job received: " + job.jobId);
                    LOG_DEBUG("  Header hash hex: " + headerHashHex.substr(0, 16) + "...");
                    LOG_DEBUG("  Target: " + std::to_string(job.target));
                    
                    if (onJob_) {
                        onJob_(job);
                    }
                }
            } else if (method == "mining.set_difficulty") {
                // Parse difficulty change
                const auto& params = message["params"];
                if (params.size() >= 1) {
                    uint64_t difficulty = params[0].get<uint64_t>();
                    LOG_INFO("Difficulty changed to: " + std::to_string(difficulty));
                    
                    if (onDifficulty_) {
                        onDifficulty_(difficulty);
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
    std::string subscriptionId_;
    MiningJob currentJob_;
    OnJobCallback onJob_;
    OnDifficultyCallback onDifficulty_;
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
