#include "ohmy/mining_engine.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/difficulty.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>

using namespace ohmy;
using namespace std::chrono_literals;

MiningEngine::MiningEngine(const Config& config)
    : config_(config)
    , deviceManager_(std::make_unique<cuda::DeviceManager>())
    , stratumClient_(std::make_unique<network::StratumClient>(config.poolUrl, config.walletAddress))
    , dagGenerator_(std::make_unique<dag::DagGenerator>())
    , startTime_(std::chrono::steady_clock::now())
    , lastLoggedJobId_("")
{
    // Initialize target to maximum (will be updated by pool)
    std::memset(currentTarget_, 0xFF, 32);
    
    // Configure DAG generator
    dagGenerator_->setCacheDir(config.cacheDir);
}

MiningEngine::~MiningEngine() {
    // RAII: unique_ptr automatically cleans up components
}

bool MiningEngine::initialize() {
    LOG_INFO("Initializing mining engine...");
    
    // Get device info
    auto devices = deviceManager_->getDevices();
    if (devices.empty()) {
        LOG_ERROR("No CUDA devices found");
        return false;
    }
    
    if (config_.deviceId >= static_cast<int>(devices.size())) {
        LOG_ERROR("Invalid device ID: " + std::to_string(config_.deviceId));
        return false;
    }
    
    const auto& device = devices[config_.deviceId];
    LOG_INFO("GPU : " + device.name);
    LOG_INFO("ALGO: etchash");
    
    // Connect to pool
    if (!stratumClient_->connect()) {
        LOG_ERROR("Failed to connect to pool");
        return false;
    }
    
    // Subscribe to mining
    if (!stratumClient_->subscribe()) {
        LOG_ERROR("Failed to subscribe to pool");
        return false;
    }
    
    // Authorize worker
    if (!stratumClient_->authorize(config_.workerName)) {
        LOG_ERROR("Failed to authorize with pool");
        return false;
    }
    
    // Set up callbacks
    stratumClient_->setOnJob([this](const MiningJob& job) {
        onJobReceived(job);
    });
    
    stratumClient_->setOnDifficulty([this](uint64_t difficulty) {
        onDifficultyUpdate(difficulty);
    });
    
    LOG_INFO("Mining engine initialized successfully");
    return true;
}

void MiningEngine::onJobReceived(const MiningJob& job) {
    currentJob_ = job;
    hasJob_ = true;
    LOG_DEBUG("New job: " + job.jobId.substr(0, 16) + "...");
    
    // Initialize DAG if epoch changed
    if (job.epoch != currentDagEpoch_) {
        if (!initializeDag(job.epoch)) {
            LOG_ERROR("Failed to initialize DAG for epoch " + std::to_string(job.epoch));
            hasJob_ = false;
        }
    }
}

void MiningEngine::onDifficultyUpdate(uint64_t difficulty) {
    lastDifficulty_ = difficulty;
    
    // Check for debug target override
    static bool checkedDebugOverride = false;
    static bool debugTargetOverride = false;
    
    if (!checkedDebugOverride) {
        if (const char* dbgTgt = std::getenv("OHMY_DEBUG_TARGET_HEX")) {
            std::string hex = dbgTgt;
            if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0) {
                hex = hex.substr(2);
            }
            if (hex.size() == 64) {
                for (int i = 0; i < 32; ++i) {
                    std::string byteStr = hex.substr(i * 2, 2);
                    currentTarget_[i] = static_cast<uint8_t>(std::strtoul(byteStr.c_str(), nullptr, 16));
                }
                debugTargetOverride = true;
                LOG_WARN("⚠️  Using debug target override from OHMY_DEBUG_TARGET_HEX");
            }
        }
        checkedDebugOverride = true;
    }
    
    if (!debugTargetOverride) {
        // Calculate target from difficulty
        utils::targetFromDifficulty(difficulty, currentTarget_);
    }
    
    std::stringstream tgt;
    tgt << "Difficulty: " << difficulty;
    LOG_INFO(tgt.str());
}

bool MiningEngine::initializeDag(uint32_t epoch) {
    LOG_INFO("Generating DAG for epoch " + std::to_string(epoch) + "...");
    
    // GPU-FIRST: Generate DAG directly in GPU memory
    const void* dagData = dagGenerator_->generate(epoch, true);
    size_t dagSize = dagGenerator_->getSize();
    void* d_dag = dagGenerator_->getGpuPointer();
    
    if (dagData == nullptr || dagSize == 0 || d_dag == nullptr) {
        LOG_ERROR("Failed to generate DAG for epoch " + std::to_string(epoch));
        return false;
    }
    
    // Zero-copy: Use GPU pointer directly (no H→D copy)
    if (!deviceManager_->initDeviceZeroCopy(config_.deviceId, d_dag, dagSize)) {
        LOG_ERROR("Failed to init GPU with DAG epoch " + std::to_string(epoch));
        return false;
    }
    
    // CRITICAL: Free host RAM placeholder (DAG is GPU-only)
    dagGenerator_->freeHostMemory();
    
    LOG_INFO("DAG ready, epoch " + std::to_string(epoch) + " (" + 
             std::to_string(dagSize / (1024*1024)) + " MB) [GPU-resident, zero-copy]");
    
    currentDagEpoch_ = epoch;
    return true;
}

void MiningEngine::processSolutions(const std::vector<Solution>& solutions, const std::string& jobId) {
    if (solutions.empty()) {
        return;
    }
    
    LOG_INFO("Found " + std::to_string(solutions.size()) + " solution(s)");
    
    // Submit each solution
    for (const auto& solution : solutions) {
        // Create solution with correct job ID
        Solution submittingSolution = solution;
        submittingSolution.jobId = jobId;
        
        // Log solution details
        std::stringstream mixLog;
        mixLog << "Solution nonce=" << std::hex << solution.nonce << " mix=";
        for (int i = 0; i < 8 && i < 32; ++i) {
            mixLog << std::hex << std::setw(2) << std::setfill('0') 
                   << (int)solution.mixHash[i];
        }
        mixLog << "...";
        LOG_DEBUG(mixLog.str());
        
        // Submit to pool
        if (stratumClient_->submitSolution(submittingSolution)) {
            acceptedShares_++;
            LOG_INFO("✓ Share accepted (#" + std::to_string(acceptedShares_) + ")");
        } else {
            rejectedShares_++;
            LOG_WARN("✗ Share rejected (#" + std::to_string(rejectedShares_) + ")");
        }
    }
}

void MiningEngine::run(std::atomic<bool>& runFlag) {
    LOG_INFO("Mining started");
    
    auto lastStatsTime = std::chrono::steady_clock::now();
    auto lastMessageCheck = std::chrono::steady_clock::now();
    
    while (runFlag && stratumClient_->isConnected()) {
        auto now = std::chrono::steady_clock::now();

        // Check for messages from pool every 100ms
        if (now - lastMessageCheck > 100ms) {
            stratumClient_->processMessages();
            lastMessageCheck = now;
        }

        // Wait for first job
        if (!hasJob_) {
            // Check for shutdown signal during job wait
            if (!runFlag) break;

            static auto lastWaitLog = std::chrono::steady_clock::now();
            if (now - lastWaitLog > 5s) {
                LOG_INFO("⏳ Waiting for mining job from pool...");
                lastWaitLog = now;
            }
            std::this_thread::sleep_for(100ms);
            continue;
        }

        // Wait for difficulty
        if (lastDifficulty_ == 0) {
            // Check for shutdown signal during difficulty wait
            if (!runFlag) break;

            static auto lastNoDiffLog = std::chrono::steady_clock::now();
            if (now - lastNoDiffLog > 5s) {
                LOG_INFO("Waiting for mining.set_difficulty...");
                lastNoDiffLog = now;
            }
            std::this_thread::sleep_for(250ms);
            continue;
        }

        // Snapshot job data for this search iteration
        MiningJob jobSnapshot = currentJob_;
        std::string jobId = jobSnapshot.jobId;
        hash32_t headerHash = jobSnapshot.headerHash;

        // Debug: log job details
        if (jobId != lastLoggedJobId_) {
            std::stringstream jobLog;
            jobLog << "Mining job " << jobId << " - Header: ";
            for (int i = 0; i < 8; i++) {
                jobLog << std::hex << std::setw(2) << std::setfill('0') << (int)headerHash[i];
            }
            jobLog << "... Seed: ";
            for (int i = 0; i < 8; i++) {
                jobLog << std::hex << std::setw(2) << std::setfill('0') << (int)jobSnapshot.seedHash[i];
            }
            jobLog << "...";
            LOG_DEBUG(jobLog.str());

            // Debug: log target
            std::stringstream targetLog;
            targetLog << "Target (256-bit): ";
            for (int i = 0; i < 32; i++) {
                targetLog << std::hex << std::setw(2) << std::setfill('0') << (int)currentTarget_[i];
            }
            LOG_DEBUG(targetLog.str());

            lastLoggedJobId_ = jobId;
        }

        // Mining batch parameters
        uint64_t startNonce = 0; // TODO: Replace with actual nonce logic
        uint64_t searchRange = 1000000; // TODO: Replace with dynamic batch size logic

        // Mine a batch using async search
        std::vector<Solution> solutions;
        
        deviceManager_->search(
            headerHash,
            jobSnapshot.seedHash,
            currentTarget_,
            startNonce,
            searchRange,
            solutions
        );

        // Process any solutions found
        if (!solutions.empty()) {
            LOG_INFO("Found " + std::to_string(solutions.size()) + " solution(s) in this batch!");
            processSolutions(solutions, jobId);
        }

        // Increment hashes for this batch
        totalHashes_ += searchRange;

        // Print stats every 30 seconds
        if (now - lastStatsTime > 30s) {
            auto stats = getStatistics();
            double hashrateMH = stats.hashRate / 1000000.0;

            std::stringstream statLog;
            statLog << "Mining at stratum+tcp://" << config_.poolUrl
                   << ", diff: ";

            if (lastDifficulty_ >= 1e9) {
                statLog << std::fixed << std::setprecision(2)
                       << (lastDifficulty_ / 1e9) << " G";
            } else {
                statLog << std::fixed << std::setprecision(0)
                       << lastDifficulty_;
            }

            LOG_INFO(statLog.str());

            // Get per-device hashrate
            uint64_t deviceHashrate = deviceManager_->getHashRate(config_.deviceId);
            double deviceMH = deviceHashrate / 1000000.0;

            auto devices = deviceManager_->getDevices();
            if (config_.deviceId < static_cast<int>(devices.size())) {
                std::stringstream devLog;
                devLog << "GPU #" << config_.deviceId << ": "
                       << devices[config_.deviceId].name << " - "
                       << std::fixed << std::setprecision(2) << deviceMH << " MH/s";
                LOG_INFO(devLog.str());
            }

            lastStatsTime = now;
        }

        // Minimal sleep to allow searchAsync to manage back-pressure
        std::this_thread::sleep_for(1us);
    }

    // Ensure any device mining threads are stopped cleanly before exiting
    if (deviceManager_) {
        deviceManager_->stopAllMining();
    }

    LOG_INFO("Mining stopped");
}


MiningStats MiningEngine::getStatistics() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
    
    MiningStats stats;
    stats.totalHashes = totalHashes_;
    stats.acceptedShares = acceptedShares_;
    stats.rejectedShares = rejectedShares_;
    stats.uptime = static_cast<double>(elapsed);
    stats.hashRate = (elapsed > 0) ? (totalHashes_ / elapsed) : 0;
    
    return stats;
}

bool MiningEngine::isConnected() const {
    return stratumClient_ && stratumClient_->isConnected();
}
