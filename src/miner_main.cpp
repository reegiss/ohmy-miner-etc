#include "ohmy/mining_engine.hpp"
#include "ohmy/logger.hpp"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <atomic>

using namespace ohmy;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        // Avoid calling non-async-signal-safe functions (like logging) from a signal handler.
        // Set the atomic flag and write a short message using the async-signal-safe write().
        const char msg[] = "Shutdown signal received, stopping miner...\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        g_running = false;
    }
}

void printBanner() {
    std::cout << "OhMy Miner ETC v1.0 - Ethereum Classic GPU Miner" << std::endl;
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
    std::string cacheDir = "./dag-cache";
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
    
    // Print banner
    printBanner();
    
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Set log level
    if (config.verbose) {
        utils::Logger::setLevel(utils::LogLevel::DEBUG);
    }
    
    LOG_INFO("OhMy Miner ETC v1.0");
    LOG_INFO("");
    LOG_INFO("URL : stratum+tcp://" + config.poolUrl);
    LOG_INFO("USER: " + config.walletAddress);
    if (!config.workerName.empty()) {
        LOG_INFO("WRK : " + config.workerName);
    }
    LOG_INFO("");
    
    try {
        // Create mining engine configuration
        MiningEngine::Config engineConfig;
        engineConfig.poolUrl = config.poolUrl;
        engineConfig.walletAddress = config.walletAddress;
        engineConfig.workerName = config.workerName;
        engineConfig.deviceId = config.deviceId;
        engineConfig.cacheDir = config.cacheDir;
        
        // Create and initialize mining engine
        MiningEngine engine(engineConfig);
        
        if (!engine.initialize()) {
            LOG_ERROR("Failed to initialize mining engine");
            return 1;
        }
        
        // Run main mining loop (blocks until g_running = false)
        engine.run(g_running);

        // Print final statistics
        auto stats = engine.getStatistics();
        LOG_INFO("\n═══════════════════════════════════════════════════");
        LOG_INFO("  Final Statistics:");
        LOG_INFO("  Hashrate: " + std::to_string(stats.hashRate / 1000000) + " MH/s");
        LOG_INFO("  Total Hashes: " + std::to_string(stats.totalHashes));
        LOG_INFO("  Accepted Shares: " + std::to_string(stats.acceptedShares));
        LOG_INFO("  Rejected Shares: " + std::to_string(stats.rejectedShares));
        LOG_INFO("  Uptime: " + std::to_string(static_cast<int>(stats.uptime)) + "s");
        LOG_INFO("═══════════════════════════════════════════════════");
        LOG_INFO("Thank you for mining with OhMy Miner! 🎉");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}
