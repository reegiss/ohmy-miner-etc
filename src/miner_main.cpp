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
    // Minimal banner to align with T-Rex style (keep it short & professional)
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
    
    // Print concise banner (no large ASCII art)
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
        // Track program start time for uptime
        auto programStart = std::chrono::steady_clock::now();
        
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
        LOG_INFO("GPU : " + device.name);
        LOG_INFO("ALGO: etchash");
        
        // Prepare DAG generator (epoch will be derived from first job's seed hash)
        dag::DagGenerator dagGenerator;
        dagGenerator.setCacheDir("./dag-cache");
        
        // Initialize Stratum client
        network::StratumClient stratumClient(config.poolUrl, config.walletAddress);
        
        // Phase 4.3: Register StratumClient with GPU for async result submission
        deviceManager.setResultCallback(&stratumClient);
        
        // Connect to pool
        if (!stratumClient.connect()) {
            LOG_ERROR("Failed to connect to pool");
            return 1;
        }
        
        // Subscribe to mining
        if (!stratumClient.subscribe()) {
            LOG_ERROR("Failed to subscribe to pool");
            return 1;
        }
        
        // Authorize worker
        if (!stratumClient.authorize(config.workerName)) {
            LOG_ERROR("Failed to authorize with pool");
            return 1;
        }
        
        // Set up job callback
        std::atomic<bool> hasJob{false};
        MiningJob currentJob;
        
        stratumClient.setOnJob([&](const MiningJob& job) {
            currentJob = job;
            hasJob = true;
            LOG_DEBUG("New job: " + job.jobId.substr(0, 16) + "...");
            // Initialize DAG lazily when first job arrives or epoch changes
            static uint32_t currentDagEpoch = UINT32_MAX;
            if (job.epoch != currentDagEpoch) {
                LOG_INFO("Generating DAG for epoch " + std::to_string(job.epoch) + "...");
                // GPU-FIRST: Generate DAG directly in GPU memory
                const void* dagData = dagGenerator.generate(job.epoch, true);
                size_t dagSize = dagGenerator.getSize();
                void* d_dag = dagGenerator.getGpuPointer();
                
                if (dagData == nullptr || dagSize == 0 || d_dag == nullptr) {
                    LOG_ERROR("Failed to generate DAG for epoch " + std::to_string(job.epoch));
                    g_running = false;
                    return;
                }
                
                // Zero-copy: Use GPU pointer directly (no H→D copy)
                if (!deviceManager.initDeviceZeroCopy(config.deviceId, d_dag, dagSize)) {
                    LOG_ERROR("Failed to init GPU with DAG epoch " + std::to_string(job.epoch));
                    g_running = false;
                    return;
                }
                
                // CRITICAL: Free host RAM placeholder (DAG is GPU-only)
                dagGenerator.freeHostMemory();
                
                LOG_INFO("DAG ready, epoch " + std::to_string(job.epoch) + " (" + std::to_string(dagSize / (1024*1024)) + " MB) [GPU-resident, zero-copy]");
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
            tgt << "Difficulty: " << difficulty;
            LOG_INFO(tgt.str());
        });
        
        LOG_INFO("Mining started");
        
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
            
            // Phase 4.3: Update GPU device with current job context for async callback
            deviceManager.setMiningJobContext(miningJobId, jobSnapshot.epoch);
            
            // PHASE 6: Use async pipeline tick-based search (non-blocking, multi-stream)
            // This replaces the sync search() with searchAsync() for better GPU utilization
            // The function manages N streams with overlapping compute/transfer operations
            std::vector<Solution> solutions;
            uint32_t numFound = deviceManager.searchAsync(
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
            
            // Note: totalHashes is updated inside searchAsync (counts only completed work)

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
                    LOG_INFO("Job changed, discarding " + std::to_string(numFound) + " solutions");
                } else {
                    LOG_INFO("Share found! Submitting " + std::to_string(numFound) + " solution(s)...");
                    
                    if (debugTargetOverride) {
                        LOG_WARN("Debug target override active: NOT submitting shares to pool");
                    }
                    
                    // Submit shares one by one, checking if job changed before each submission
                    uint32_t submitted = 0;
                    for (auto& solution : solutions) {
                        if (debugTargetOverride) {
                            continue; // skip submission when debug target is active
                        }
                        // Check if job changed before submitting
                        if (currentJob.jobId != miningJobId) {
                            LOG_WARN("Job changed during submission, discarding remaining " + std::to_string(numFound - submitted) + " shares");
                            break;
                        }
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
            
            // Print stats every 30 seconds (T-Rex style)
            if (now - lastStatsTime > 30s) {
                double hashRate = deviceManager.getHashRate(config.deviceId);
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - programStart).count();
                
                // Format uptime like T-Rex
                std::string uptimeStr;
                if (uptime < 60) {
                    uptimeStr = std::to_string(uptime) + " sec" + (uptime > 1 ? "s" : "");
                } else if (uptime < 3600) {
                    int mins = uptime / 60;
                    int secs = uptime % 60;
                    uptimeStr = std::to_string(mins) + " min" + (mins > 1 ? "s" : "") + " " + std::to_string(secs) + " sec" + (secs > 1 ? "s" : "");
                } else {
                    int hours = uptime / 3600;
                    int mins = (uptime % 3600) / 60;
                    int secs = uptime % 60;
                    uptimeStr = std::to_string(hours) + " hr" + (hours > 1 ? "s" : "") + " " + std::to_string(mins) + " min" + (mins > 1 ? "s" : "");
                }
                
                // Get difficulty in G format
                // Note: difficulty from pool is in units of 2^32 nonces per share
                // For display: show as difficulty (no conversion for small values like 2)
                // For large values: show in G (gigahashes = 1e9)
                double diffValue = static_cast<double>(lastDifficulty);
                std::ostringstream diffStream;
                if (diffValue >= 1e9) {
                    diffStream << std::fixed << std::setprecision(2) << (diffValue / 1e9) << " G";
                } else {
                    diffStream << std::fixed << std::setprecision(0) << diffValue;
                }
                
                // Format hashrate in MH/s
                std::ostringstream hashStream;
                hashStream << std::fixed << std::setprecision(2) << (hashRate / 1e6);
                
                // T-Rex style separator line and stats
                std::cout << "\n---------------------";
                auto t = std::time(nullptr);
                auto tm = *std::localtime(&t);
                std::cout << std::put_time(&tm, "%Y%m%d %H:%M:%S");
                std::cout << " ---------------------\n";
                
                std::ostringstream poolLine;
                poolLine << "Mining at " << config.poolUrl << ", diff: " << diffStream.str();
                LOG_INFO(poolLine.str());
                
                std::ostringstream gpuLine;
                gpuLine << "GPU #0: " << devices[0].name << " - " << hashStream.str() << " MH/s";
                LOG_INFO(gpuLine.str());
                
                LOG_INFO("Shares/min: 0");
                LOG_INFO("Uptime: " + uptimeStr + " | Algo: etchash | OhMy-Miner v1.0");
                
                lastStatsTime = now;
            }
            
            // PHASE 6: Minimal sleep (1us) - async pipeline manages back-pressure via cudaEventQuery
            // The searchAsync() function is non-blocking and uses event queries to pace the loop
            // CPU can spin very fast since most time is spent on GPU kernels executing in parallel
            std::this_thread::sleep_for(1us);

            // Add CPU usage throttling to reduce high CPU load
            static int throttleCounter = 0;
            if (++throttleCounter % 1000 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            // Reduce CPU load by adding a sleep interval in the mining loop
            static int miningLoopCounter = 0;
            if (++miningLoopCounter % 500 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
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
