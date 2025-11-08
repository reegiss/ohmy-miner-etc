#include "ohmy/device_manager.hpp"
#include "ohmy/stratum_client.hpp"
#include "ohmy/dag_generator.hpp"
#include "ohmy/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

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
        
        // Generate DAG for epoch 0 (ETC)
        LOG_INFO("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        LOG_INFO("Initializing DAG...");
        LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        
        dag::DagGenerator dagGenerator;
        dagGenerator.setCacheDir("./dag-cache");
        
        const uint32_t epoch = 0; // Start with epoch 0
        const void* dagData = dagGenerator.generate(epoch, false); // CPU generation for now
        size_t dagSize = dagGenerator.getSize();
        
        if (dagData == nullptr) {
            LOG_ERROR("Failed to generate DAG");
            return 1;
        }
        
        LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        LOG_INFO("✓ DAG initialized successfully");
        LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        
        // Initialize device with DAG
        if (!deviceManager.initDevice(config.deviceId, dagData, dagSize)) {
            LOG_ERROR("Failed to initialize GPU with DAG");
            return 1;
        }
        LOG_INFO("✓ GPU initialized with DAG");
        
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
        });
        
        stratumClient.setOnDifficulty([](uint64_t difficulty) {
            LOG_INFO("⚙️  Difficulty updated: " + std::to_string(difficulty));
        });
        
        LOG_INFO("\n🚀 Mining started! Press Ctrl+C to stop.\n");
        
        // Main mining loop
        auto lastStatsTime = std::chrono::steady_clock::now();
        auto lastMessageCheck = std::chrono::steady_clock::now();
        uint64_t totalHashes = 0;
        uint32_t acceptedShares = 0;
        
        while (g_running && stratumClient.isConnected()) {
            // Check for messages from pool every 100ms
            auto now = std::chrono::steady_clock::now();
            if (now - lastMessageCheck > 100ms) {
                stratumClient.processMessages();
                lastMessageCheck = now;
            }
            
            // Wait for first job
            if (!hasJob) {
                std::this_thread::sleep_for(100ms);
                continue;
            }
            
            // Mine for a short burst
            const uint64_t startNonce = totalHashes;
            const uint64_t searchRange = 1000000; // Search 1M nonces
            
            // Use real job data if available
            hash32_t headerHash = currentJob.headerHash;
            uint64_t target = currentJob.target;
            
            // If no valid target, use high value for testing
            if (target == 0) {
                target = 0xFFFFFFFFFFFFFFFF;
            }
            
            std::vector<Solution> solutions;
            uint32_t numFound = deviceManager.search(
                headerHash,
                target,
                startNonce,
                searchRange,
                solutions
            );
            
            totalHashes += searchRange;
            
            if (numFound > 0) {
                LOG_INFO("💎 Found " + std::to_string(numFound) + " solution(s)!");
                
                for (const auto& solution : solutions) {
                    LOG_INFO("   Nonce: " + std::to_string(solution.nonce));
                    
                    // Submit solution to pool
                    if (stratumClient.submitSolution(solution)) {
                        acceptedShares++;
                        LOG_INFO("✓ Share accepted (" + std::to_string(acceptedShares) + " total)");
                    } else {
                        LOG_WARN("✗ Share rejected");
                    }
                }
            }
            
            // Print stats every 10 seconds
            if (now - lastStatsTime > 10s) {
                double hashRate = deviceManager.getHashRate(config.deviceId);
                
                LOG_INFO("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                LOG_INFO("📊 Mining Statistics:");
                LOG_INFO("   Hash Rate: " + std::string(hashRate >= 1e6 ? 
                    std::to_string(hashRate / 1e6) + " MH/s" :
                    std::to_string(hashRate / 1e3) + " KH/s"));
                LOG_INFO("   Total Hashes: " + std::to_string(totalHashes));
                LOG_INFO("   Accepted Shares: " + std::to_string(acceptedShares));
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
