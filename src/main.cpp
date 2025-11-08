#include "ohmy/miner.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/config.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

std::atomic<bool> shouldStop{false};

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    shouldStop = true;
}

int main(int argc, char** argv) {
    try {
        // Parse command line arguments
        if (!ohmy::utils::Config::parseArgs(argc, argv)) {
            std::cerr << "Failed to parse arguments\n";
            return 1;
        }

        // Initialize logger
        auto generalConfig = ohmy::utils::Config::getGeneralConfig();
        ohmy::utils::Logger::init(
            ohmy::utils::LogLevel::INFO,
            generalConfig.logFile
        );

        LOG_INFO("=== OhMy Miner ETC v1.0.0 ===");
        LOG_INFO("Starting Ethereum Classic miner...");

        // Register signal handler
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Create and start miner
        ohmy::Miner miner("config.json");
        
        // Set callbacks
        miner.setOnSolution([](const ohmy::Solution& solution) {
            LOG_INFO("Solution found! Nonce: " + std::to_string(solution.nonce));
        });

        miner.setOnStats([](const ohmy::MiningStats& stats) {
            LOG_INFO("Hash rate: " + std::to_string(stats.hashRate / 1000000.0) + " MH/s");
        });

        // Start mining
        if (!miner.start()) {
            LOG_ERROR("Failed to start miner");
            return 1;
        }

        LOG_INFO("Mining started. Press Ctrl+C to stop.");

        // Main loop
        while (!shouldStop && miner.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Stop miner
        LOG_INFO("Stopping miner...");
        miner.stop();

        // Print final stats
        auto stats = miner.getStats();
        LOG_INFO("Final statistics:");
        LOG_INFO("  Total hashes: " + std::to_string(stats.totalHashes));
        LOG_INFO("  Accepted shares: " + std::to_string(stats.acceptedShares));
        LOG_INFO("  Rejected shares: " + std::to_string(stats.rejectedShares));
        LOG_INFO("  Uptime: " + std::to_string(stats.uptime) + " seconds");

        LOG_INFO("Miner stopped successfully.");
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
