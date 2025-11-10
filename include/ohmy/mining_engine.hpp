#pragma once

#include "types.hpp"
#include "device_manager.hpp"
#include "stratum_client.hpp"
#include "dag_generator.hpp"
#include <memory>
#include <atomic>
#include <functional>
#include <chrono>

namespace ohmy {

/**
 * @brief Mining Engine - Orchestrates mining operations
 * 
 * This class is the central orchestrator that coordinates between:
 * - Network layer (StratumClient)
 * - Computation layer (DeviceManager)
 * - DAG generation (DagGenerator)
 * 
 * Responsibilities:
 * - Owns and manages lifetime of core components
 * - Implements main mining loop (run method)
 * - Coordinates job distribution and solution submission
 * - Maintains mining state (current job, target, statistics)
 * 
 * Architecture principles:
 * - Dependency Inversion: High-level orchestrator depends on abstractions
 * - Single Responsibility: Only orchestration logic, no low-level details
 * - Separation of Concerns: Network, compute, and control are decoupled
 */
class MiningEngine {
public:
    /**
     * @brief Configuration for mining engine
     */
    struct Config {
        std::string poolUrl;
        std::string walletAddress;
        std::string workerName;
        int deviceId = 0;
        std::string cacheDir = "./dag-cache";
    };

    /**
     * @brief Construct mining engine with configuration
     * @param config Mining configuration
     */
    explicit MiningEngine(const Config& config);
    
    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~MiningEngine();

    // Disable copy and move
    MiningEngine(const MiningEngine&) = delete;
    MiningEngine& operator=(const MiningEngine&) = delete;
    MiningEngine(MiningEngine&&) = delete;
    MiningEngine& operator=(MiningEngine&&) = delete;

    /**
     * @brief Initialize engine components
     * - Connect to pool
     * - Subscribe and authorize
     * - Setup callbacks
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Main mining loop
     * Runs until stop flag is set or error occurs
     * @param runFlag Atomic flag to control loop execution
     */
    void run(std::atomic<bool>& runFlag);

    /**
     * @brief Get current mining statistics
     */
    MiningStats getStatistics() const;

    /**
     * @brief Check if engine is connected to pool
     */
    bool isConnected() const;

private:
    // Configuration
    Config config_;

    // Owned components (Dependency Injection via composition)
    std::unique_ptr<cuda::DeviceManager> deviceManager_;
    std::unique_ptr<network::StratumClient> stratumClient_;
    std::unique_ptr<dag::DagGenerator> dagGenerator_;

    // Mining state
    std::atomic<bool> hasJob_{false};
    MiningJob currentJob_;
    uint8_t currentTarget_[32];
    uint64_t lastDifficulty_{0};
    uint32_t currentDagEpoch_{UINT32_MAX};

    // Statistics
    uint64_t totalHashes_{0};
    uint32_t acceptedShares_{0};
    uint32_t rejectedShares_{0};
    std::chrono::steady_clock::time_point startTime_;

    // Logging state
    std::string lastLoggedJobId_;

    // Internal methods
    
    /**
     * @brief Handle new job from pool
     */
    void onJobReceived(const MiningJob& job);

    /**
     * @brief Handle difficulty update from pool
     */
    void onDifficultyUpdate(uint64_t difficulty);

    /**
     * @brief Generate and initialize DAG for epoch
     * @return true if successful
     */
    bool initializeDag(uint32_t epoch);

    /**
     * @brief Process and submit solutions found by GPU
     */
    void processSolutions(const std::vector<Solution>& solutions, const std::string& jobId);
};

} // namespace ohmy
