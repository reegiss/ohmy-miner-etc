#include "ohmy/device_manager.hpp"
#include "ohmy/stratum_client.hpp"
#include "ohmy/dag_generator.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/ethash.hpp"
#include "ohmy/difficulty.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstring>
#include <sstream>
#include <iomanip>

using namespace ohmy;
using namespace std::chrono_literals;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Shutdown signal received, stopping miner...");
        g_running = false;
    }
}

void printBanner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║        ██████╗ ██╗  ██╗███╗   ███╗██╗   ██╗              ║
║       ██╔═══██╗██║  ██║████╗ ████║╚██╗ ██╔╝              ║
║       ██║   ██║███████║██╔████╔██║ ╚████╔╝               ║
║       ██║   ██║██╔══██║██║╚██╔╝██║  ╚██╔╝                ║
║       ╚██████╔╝██║  ██║██║ ╚═╝ ██║   ██║                 ║
║        ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝   ╚═╝                 ║
║                                                           ║
║          Ethereum Classic GPU Miner v1.0                 ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n\n"
              << "Required options:\n"
              << "  --pool <url:port>       Mining pool URL and port\n"
              << "  --wallet <address>      ETC wallet address\n\n"
              << "Optional:\n"
              << "  --worker <name>         Worker name (default: empty)\n"
              << "  --device <id>           GPU device ID (default: 0)\n"
              << "  --verbose               Enable verbose logging\n"
              << "  --help                  Show this help message\n\n"
              << "Example:\n"
              << "  " << programName << " --pool eu1-etc.ethermine.org:4444 \\\n"
              << "                  --wallet 0x1234...abcd --worker rig1\n"
              << std::endl;
}

struct Config {
    std::string poolUrl;
    std::string walletAddress;
    std::string workerName;
    int deviceId = 0;
    bool verbose = false;
};

bool parseArgs(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            return false;
        } else if (arg == "--pool" && i + 1 < argc) {
            config.poolUrl = argv[++i];
        } else if (arg == "--wallet" && i + 1 < argc) {
            config.walletAddress = argv[++i];
        } else if (arg == "--worker" && i + 1 < argc) {
            config.workerName = argv[++i];
        } else if (arg == "--device" && i + 1 < argc) {
            config.deviceId = std::stoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            return false;
        }
    }
    
    // Validate required options
    if (config.poolUrl.empty()) {
        std::cerr << "Error: --pool is required\n";
        return false;
    }
    if (config.walletAddress.empty()) {
        std::cerr << "Error: --wallet is required\n";
        return false;
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    Config config;
    if (!parseArgs(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }
    
    printBanner();
    
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Set log level
    if (config.verbose) {
        utils::Logger::setLevel(utils::LogLevel::DEBUG);
    }
    
    LOG_INFO("═══════════════════════════════════════════════════");
    LOG_INFO("  OhMy Miner ETC - Starting...");
    LOG_INFO("═══════════════════════════════════════════════════");
    LOG_INFO("Pool: " + config.poolUrl);
    LOG_INFO("Wallet: " + config.walletAddress);
    if (!config.workerName.empty()) {
        LOG_INFO("Worker: " + config.workerName);
    }
    LOG_INFO("Device: " + std::to_string(config.deviceId));
    LOG_INFO("═══════════════════════════════════════════════════");
    
    try {
        // Initialize CUDA device
        LOG_INFO("Initializing GPU device " + std::to_string(config.deviceId) + "...");
        cuda::DeviceManager deviceManager;
        
        // Get device info
        auto devices = deviceManager.getDevices();
        if (devices.empty()) {
            LOG_ERROR("No CUDA devices found");
            return 1;
        }
        
        if (config.deviceId >= static_cast<int>(devices.size())) {
            LOG_ERROR("Invalid device ID: " + std::to_string(config.deviceId));
            return 1;
        }
        
        const auto& device = devices[config.deviceId];
        LOG_INFO("Using device: " + device.name);
        LOG_INFO("  Compute Capability: " + std::to_string(device.computeCapability / 10) + "." + 
                 std::to_string(device.computeCapability % 10));
        LOG_INFO("  Memory: " + std::to_string(device.totalMemory / (1024*1024)) + " MB");
        LOG_INFO("  SMs: " + std::to_string(device.multiProcessorCount));
        
        // Prepare DAG generator (epoch will be derived from first job's seed hash)
        dag::DagGenerator dagGenerator;
        dagGenerator.setCacheDir("./dag-cache");
        LOG_INFO("DAG generator ready (will build on first job epoch)");
        
        // Initialize Stratum client
        LOG_INFO("Connecting to pool...");
        network::StratumClient stratumClient(config.poolUrl, config.walletAddress);
        
        // Connect to pool
        if (!stratumClient.connect()) {
            LOG_ERROR("Failed to connect to pool");
            return 1;
        }
        LOG_INFO("✓ Connected to pool");
        
        // Subscribe to mining
        if (!stratumClient.subscribe()) {
            LOG_ERROR("Failed to subscribe to pool");
            return 1;
        }
        LOG_INFO("✓ Subscribed to pool");
        
        // Authorize worker
        if (!stratumClient.authorize(config.workerName)) {
            LOG_ERROR("Failed to authorize with pool");
            return 1;
        }
        LOG_INFO("✓ Authorized with pool");
        
        // Set up job callback
        std::atomic<bool> hasJob{false};
        MiningJob currentJob;
        
        stratumClient.setOnJob([&](const MiningJob& job) {
            currentJob = job;
            hasJob = true;
            LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            LOG_INFO("📋 New mining job: " + job.jobId);
            LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            // Initialize DAG lazily when first job arrives or epoch changes
            static uint32_t currentDagEpoch = UINT32_MAX;
            if (job.epoch != currentDagEpoch) {
                LOG_INFO("Epoch changed (" + std::to_string(currentDagEpoch) + " -> " + std::to_string(job.epoch) + ") - generating/loading DAG...");
                // Use GPU DAG generation for faster startup (pass true)
                const void* dagData = dagGenerator.generate(job.epoch, true);
                size_t dagSize = dagGenerator.getSize();
                if (dagData == nullptr || dagSize == 0) {
                    LOG_ERROR("Failed to generate DAG for epoch " + std::to_string(job.epoch));
                    g_running = false;
                    return;
                }
                if (!deviceManager.initDevice(config.deviceId, dagData, dagSize)) {
                    LOG_ERROR("Failed to init GPU with DAG epoch " + std::to_string(job.epoch));
                    g_running = false;
                    return;
                }
                LOG_INFO("✓ GPU initialized with DAG epoch " + std::to_string(job.epoch) + ", size " + std::to_string(dagSize / (1024*1024)) + " MB");
                currentDagEpoch = job.epoch;
            }
        });
        
    uint8_t currentTarget256[32];
    uint64_t lastDifficulty = 0;
    std::memset(currentTarget256, 0xFF, 32);
    bool debugTargetOverride = false;
    // Allow debug override via env OHMY_DEBUG_TARGET_HEX (64 hex chars)
    if (const char* dbgTgt = std::getenv("OHMY_DEBUG_TARGET_HEX")) {
        std::string hex = dbgTgt;
        if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) hex = hex.substr(2);
        if (hex.size() == 64) {
            for (int i = 0; i < 32; ++i) {
                std::string byteStr = hex.substr(i * 2, 2);
                currentTarget256[i] = static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16));
            }
            debugTargetOverride = true;
            LOG_WARN("⚠️  Using debug target override from OHMY_DEBUG_TARGET_HEX; real difficulty ignored until next set_difficulty.");
            std::stringstream tgt; tgt << "Debug Target (BE): ";
            for (int i = 0; i < 32; ++i) {
                tgt << std::hex << std::setw(2) << std::setfill('0') << (int)currentTarget256[i];
                if ((i+1)%4==0) tgt << ' ';
            }
            LOG_INFO(tgt.str());
        } else {
            LOG_WARN("OHMY_DEBUG_TARGET_HEX provided but length != 64 hex chars; ignoring.");
        }
    }

        stratumClient.setOnDifficulty([&](uint64_t difficulty) {
            lastDifficulty = difficulty;
            if (!debugTargetOverride) {
                utils::targetFromDifficulty(difficulty, currentTarget256);
            }
            std::stringstream tgt;
            tgt << "⚙️  Difficulty updated: " << difficulty << "\n     Target (BE): ";
            for (int i = 0; i < 32; ++i) {
                tgt << std::hex << std::setw(2) << std::setfill('0') << (int)currentTarget256[i];
                if ((i+1)%4==0) tgt << ' ';
            }
            LOG_INFO(tgt.str());
        });
        
        LOG_INFO("\n🚀 Mining started! Press Ctrl+C to stop.\n");
        
        // Main mining loop
        auto lastStatsTime = std::chrono::steady_clock::now();
        auto lastMessageCheck = std::chrono::steady_clock::now();
        uint64_t totalHashes = 0;
        uint32_t acceptedShares = 0;
        std::string activeJobId = ""; // Track the job ID we're actively mining
        
        while (g_running && stratumClient.isConnected()) {
            // Check for messages from pool every 100ms
            auto now = std::chrono::steady_clock::now();
            if (now - lastMessageCheck > 100ms) {
                stratumClient.processMessages();
                lastMessageCheck = now;
            }
            
            // Wait for first job
            if (!hasJob) {
                // Log waiting status every 5 seconds
                static auto lastWaitLog = std::chrono::steady_clock::now();
                if (now - lastWaitLog > 5s) {
                    LOG_INFO("⏳ Waiting for mining job from pool...");
                    lastWaitLog = now;
                }
                std::this_thread::sleep_for(100ms);
                continue;
            }
            
            // Delay mining until we have a real difficulty (avoid all-FF target flood)
            if (lastDifficulty == 0) {
                static auto lastNoDiffLog = std::chrono::steady_clock::now();
                if (now - lastNoDiffLog > 5s) {
                    LOG_INFO("Waiting for mining.set_difficulty before starting hash search...");
                    lastNoDiffLog = now;
                }
                std::this_thread::sleep_for(250ms);
                continue;
            }

            // Mine for a short burst
            const uint64_t startNonce = totalHashes;
            const uint64_t searchRange = 262144; // Smaller batch to reduce stale shares (256K)
            
            // Snapshot job data BEFORE starting GPU search
            MiningJob jobSnapshot = currentJob;
            std::string miningJobId = jobSnapshot.jobId;
            hash32_t headerHash = jobSnapshot.headerHash;
            
            // Log header for first batch only (avoid spam)
            static bool loggedHeader = false;
            if (!loggedHeader) {
                std::string headerHex = "";
                for (int i = 0; i < 32; i++) {
                    char buf[3];
                    snprintf(buf, 3, "%02x", headerHash.data()[i]);
                    headerHex += buf;
                }
                LOG_INFO("Mining header: " + headerHex);
                loggedHeader = true;
            }
            
            // Mark this job as active
            activeJobId = miningJobId;
            
            // GPU-only full Ethash validation: kernel returns only valid shares under target
            std::vector<Solution> solutions;
            uint32_t numFound = deviceManager.search(
                headerHash,
                jobSnapshot.seedHash,    // Pass seedHash from job
                currentTarget256,
                startNonce,
                searchRange,
                solutions
            );
            
            // Debug: Log search parameters occasionally
            static uint64_t searchCounter = 0;
            searchCounter++;
            if (searchCounter % 100 == 0) {
                LOG_DEBUG("Search #" + std::to_string(searchCounter) + ": startNonce=" + std::to_string(startNonce) + 
                         ", range=" + std::to_string(searchRange) + ", found=" + std::to_string(numFound));
            }

            // If we found solutions, set jobId and log details
            if (numFound > 0) {
                // Set jobId for all solutions
                for (auto& solution : solutions) {
                    solution.jobId = miningJobId;
                }
                
                // Log compressed mix (first solution only)
                const Solution& first = solutions[0];
                std::stringstream mixLog;
                mixLog << "First solution compressed mix: ";
                for (int w = 0; w < 32; ++w) {
                    mixLog << std::hex << std::setw(2) << std::setfill('0') << (int)first.mixHash[w];
                }
                LOG_INFO(mixLog.str());
                // Also log nonce hex for transparency
                std::stringstream nss; nss << "First solution nonce: 0x" << std::hex << std::setw(16) << std::setfill('0') << first.nonce;
                LOG_INFO(nss.str());
            }
            
            totalHashes += searchRange;

            // For diagnostics: show head of target and fabricated example hash distribution sample every ~15s
            static auto lastDiag = std::chrono::steady_clock::now();
            if (now - lastDiag > 15s) {
                std::stringstream tss; tss << "Target (full): ";
                for (int i = 0; i < 32; ++i) tss << std::hex << std::setw(2) << std::setfill('0') << (int)currentTarget256[i];
                LOG_DEBUG(tss.str());
                
                // Also log in little-endian format for comparison
                std::stringstream tss_le; tss_le << "Target (LE):   ";
                for (int i = 31; i >= 0; --i) tss_le << std::hex << std::setw(2) << std::setfill('0') << (int)currentTarget256[i];
                LOG_DEBUG(tss_le.str());
                
                lastDiag = now;
            }
            
            if (numFound > 0) {
                // Set jobId for all solutions
                for (auto& solution : solutions) {
                    solution.jobId = miningJobId;
                }
                
                // If job changed during the GPU batch, discard solutions (avoid stale shares)
                if (currentJob.jobId != miningJobId) {
                    LOG_INFO("⚠️  Discarding " + std::to_string(numFound) + " solutions due to job change (" + miningJobId + " -> " + currentJob.jobId + ")");
                } else {
                    LOG_INFO("💎 Found " + std::to_string(numFound) + " valid share(s) on GPU for job " + miningJobId + "!");
                    
                    if (debugTargetOverride) {
                        LOG_WARN("Debug target override active: NOT submitting shares to pool to avoid invalid submissions.");
                    }
                    
                    // Submit shares one by one, checking if job changed before each submission
                    uint32_t submitted = 0;
                    for (auto& solution : solutions) {
                        if (debugTargetOverride) {
                            continue; // skip submission when debug target is active
                        }
                        // Check if job changed before submitting
                        if (currentJob.jobId != miningJobId) {
                            LOG_WARN("   ⚠️  Job changed to " + currentJob.jobId + " during submission, discarding remaining " + std::to_string(numFound - submitted) + " shares");
                            break;
                        }
                        LOG_INFO("   Nonce: " + std::to_string(solution.nonce) + " -> submitting to pool");
                        if (stratumClient.submitSolution(solution)) {
                            acceptedShares++;
                            LOG_INFO("✓ Share accepted (" + std::to_string(acceptedShares) + " total)");
                        } else {
                            LOG_WARN("✗ Share rejected by pool");
                        }
                        submitted++;
                    }
                }
            }
            
            // Print stats every 10 seconds
            if (now - lastStatsTime > 10s) {
                double hashRate = deviceManager.getHashRate(config.deviceId);
                // Expected time to share: difficulty * 2^32 / hashRate
                double expectedSeconds = 0.0;
                if (lastDifficulty > 0 && hashRate > 0) {
                    long double work = static_cast<long double>(lastDifficulty) * static_cast<long double>(1ULL << 32);
                    expectedSeconds = static_cast<double>(work / static_cast<long double>(hashRate));
                }
                
                LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                LOG_INFO("📊 Mining Statistics:");
                LOG_INFO("   Hash Rate: " + std::string(hashRate >= 1e6 ? 
                    std::to_string(hashRate / 1e6) + " MH/s" :
                    std::to_string(hashRate / 1e3) + " KH/s"));
                LOG_INFO("   Total Hashes: " + std::to_string(totalHashes));
                LOG_INFO("   Accepted Shares: " + std::to_string(acceptedShares));
                if (lastDifficulty > 0) {
                    LOG_INFO("   Share Difficulty: " + std::to_string(lastDifficulty));
                    if (expectedSeconds > 0.0) {
                        LOG_INFO("   Expected Avg Time/share: " + std::to_string(expectedSeconds/60.0) + " min");
                    }
                }
                LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                
                lastStatsTime = now;
            }
            
            // Small sleep to prevent tight loop
            std::this_thread::sleep_for(10ms);
        }
        
        // Cleanup
        LOG_INFO("\n🛑 Stopping miner...");
        stratumClient.disconnect();
        LOG_INFO("✓ Disconnected from pool");
        
        // Final stats
        LOG_INFO("\n═══════════════════════════════════════════════════");
        LOG_INFO("  Final Statistics:");
        LOG_INFO("  Total Hashes: " + std::to_string(totalHashes));
        LOG_INFO("  Accepted Shares: " + std::to_string(acceptedShares));
        LOG_INFO("═══════════════════════════════════════════════════");
        LOG_INFO("Thank you for mining with OhMy Miner! 🎉");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}
