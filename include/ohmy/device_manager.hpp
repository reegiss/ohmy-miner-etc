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
     * @brief Initialize device for mining
     */
    bool initDevice(int deviceId, const void* dag, size_t dagSize);

    /**
     * @brief Search for solutions on GPU
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
