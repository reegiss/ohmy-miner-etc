#pragma once

#include "types.hpp"
#include <cuda_runtime.h>
#include <memory>
#include <vector>

namespace ohmy {
namespace cuda {

/**
 * @brief GPU device information
 */
struct DeviceInfo {
    int deviceId;
    std::string name;
    size_t totalMemory;
    size_t freeMemory;
    int computeCapability;
    int multiProcessorCount;
    int maxThreadsPerBlock;
};

/**
 * @brief Manages CUDA devices and mining operations
 */
class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // Disable copy
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    /**
     * @brief Get list of available CUDA devices
     */
    std::vector<DeviceInfo> getDevices() const;

    /**
     * @brief Initialize all available CUDA devices for mining (Phase 5)
     * Auto-detects GPUs, allocates resources, and spawns mining threads
     * @return Number of devices successfully initialized
     */
    int initializeAllDevices(const void* dag, size_t dagSize);

    /**
     * @brief Initialize specific device for mining
     */
    bool initDevice(int deviceId, const void* dag, size_t dagSize);

    /**
     * @brief Initialize specific device with device offset for nonce distribution (Phase 5)
     * @param deviceId The CUDA device ID to initialize
     * @param devicesTotal Total number of devices for nonce range calculation
     * @param dag Pointer to DAG data
     * @param dagSize Size of DAG in bytes
     */
    bool initDeviceWithMultiGpu(int deviceId, int devicesTotal, 
                                const void* dag, size_t dagSize);

    /**
     * @brief Get count of initialized devices
     */
    int getDeviceCount() const;

    /**
     * @brief Check if a specific device is initialized
     */
    bool isDeviceInitialized(int deviceId) const;

    /**
     * @brief Get information for specific device
     */
    DeviceInfo getDeviceInfo(int deviceId) const;

    /**
     * @brief Search for solutions on single GPU
     * @param headerHash Block header hash
     * @param target Difficulty target
     * @param startNonce Starting nonce value
     * @param count Number of nonces to search
     * @param solutions Output vector for found solutions
     * @return Number of solutions found
     */
    uint32_t search(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t startNonce,
        uint64_t count,
        std::vector<Solution>& solutions
    );

    /**
     * @brief Search for solutions on specific device (Phase 5)
     * Used in multi-GPU mode to search on one device with device-specific nonce range
     * @param deviceId Device to search on
     * @param headerHash Block header hash
     * @param seedHash Seed hash for verification
     * @param targetBE Difficulty target (big-endian)
     * @param solutions Output vector for found solutions
     * @return Number of solutions found
     */
    uint32_t searchDevice(
        int deviceId,
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        std::vector<Solution>& solutions
    );

    /**
     * @brief Start continuous mining on all initialized devices (Phase 5)
     * Spawns per-device mining threads, each running independent search loop
     * @param headerHash Block header hash
     * @param seedHash Seed hash
     * @param targetBE Difficulty target
     * @param durationSeconds How long to mine (0 = indefinite)
     */
    void startMiningAllDevices(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t durationSeconds = 0
    );

    /**
     * @brief Stop all mining threads gracefully
     */
    void stopAllMining();

    /**
     * @brief Get total hashrate from all devices (Phase 5)
     */
    uint64_t getTotalHashRate() const;

    /**
     * @brief Get per-device hashrate vector (Phase 5)
     */
    std::vector<uint64_t> getAllHashRates() const;

    /**
     * @brief Set callback handler for async result submission
     * Used by Phase 4 async callback system to submit results to pool
     * @param stratumClient Pointer to StratumClient for pool submission
     */
    void setResultCallback(void* stratumClient);

    /**
     * @brief Set mining job ID and epoch for callback context
     * Called before each search() to provide job identification
     * @param jobId Job identifier from pool
     * @param epoch DAG epoch for mining context
     */
    void setMiningJobContext(const std::string& jobId, uint32_t epoch);

    /**
     * @brief Get current hash rate for device
     */
    uint64_t getHashRate(int deviceId) const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

/**
 * @brief Generate DAG on GPU
 * @param d_cache Device pointer to cache data
 * @param d_dag Device pointer to DAG output
 * @param numCacheItems Number of cache items
 * @param numDagItems Number of DAG items to generate
 */
void generateDagGpu(
    const void* d_cache,
    void* d_dag,
    uint32_t numCacheItems,
    uint32_t numDagItems
);

} // namespace cuda
} // namespace ohmy
