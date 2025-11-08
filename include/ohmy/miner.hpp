#pragma once

#include "types.hpp"
#include <memory>
#include <functional>

namespace ohmy {

// Forward declarations
class StratumClient;
class DeviceManager;
class DagGenerator;

/**
 * @brief Main miner class that orchestrates the mining process
 */
class Miner {
public:
    using OnSolutionCallback = std::function<void(const Solution&)>;
    using OnStatsCallback = std::function<void(const MiningStats&)>;

    explicit Miner(const std::string& configFile);
    ~Miner();

    // Disable copy
    Miner(const Miner&) = delete;
    Miner& operator=(const Miner&) = delete;

    // Enable move
    Miner(Miner&&) noexcept = default;
    Miner& operator=(Miner&&) noexcept = default;

    /**
     * @brief Start mining process
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop mining process
     */
    void stop();

    /**
     * @brief Check if miner is currently running
     */
    bool isRunning() const;

    /**
     * @brief Get current mining statistics
     */
    MiningStats getStats() const;

    /**
     * @brief Set callback for when solution is found
     */
    void setOnSolution(OnSolutionCallback callback);

    /**
     * @brief Set callback for periodic statistics updates
     */
    void setOnStats(OnStatsCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace ohmy
