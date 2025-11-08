#pragma once

#include "types.hpp"
#include <memory>
#include <functional>

namespace ohmy {
namespace network {

/**
 * @brief Stratum protocol client for pool communication
 */
class StratumClient {
public:
    using OnJobCallback = std::function<void(const MiningJob&)>;
    using OnDifficultyCallback = std::function<void(uint64_t)>;

    explicit StratumClient(const std::string& poolUrl, const std::string& walletAddress);
    ~StratumClient();

    // Disable copy
    StratumClient(const StratumClient&) = delete;
    StratumClient& operator=(const StratumClient&) = delete;

    /**
     * @brief Connect to mining pool
     */
    bool connect();

    /**
     * @brief Disconnect from pool
     */
    void disconnect();

    /**
     * @brief Check if connected to pool
     */
    bool isConnected() const;

    /**
     * @brief Subscribe to mining notifications
     */
    bool subscribe();

    /**
     * @brief Authorize with pool
     */
    bool authorize(const std::string& workerName = "");

    /**
     * @brief Submit mining solution to pool
     */
    bool submitSolution(const Solution& solution);

    /**
     * @brief Process incoming messages from pool (non-blocking)
     * @return true if messages were processed
     */
    bool processMessages();

    /**
     * @brief Set callback for new mining jobs
     */
    void setOnJob(OnJobCallback callback);

    /**
     * @brief Set callback for difficulty changes
     */
    void setOnDifficulty(OnDifficultyCallback callback);

    /**
     * @brief Get current mining job
     */
    MiningJob getCurrentJob() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace network
} // namespace ohmy
