#pragma once

#include "types.hpp"
#include <memory>

namespace ohmy {

/**
 * @brief Ethash algorithm implementation
 */
class Ethash {
public:
    /**
     * @brief Calculate cache for an epoch
     * @param epoch Epoch number
     * @return Cache as vector of 64-byte hashes
     */
    static std::vector<hash64_t> calculateCache(uint32_t epoch);
    
    /**
     * @brief FNV-1a hash function (32-bit)
     * Used for mixing in Ethash algorithm
     * @param a First value
     * @param b Second value
     * @return FNV hash of a and b
     */
    static inline uint32_t fnv1a(uint32_t a, uint32_t b) {
        return a * FNV_PRIME ^ b;
    }
    
    /**
     * @brief FNV hash for mixing arrays
     * @param mix Array to mix (modified in-place)
     * @param data Data to mix in
     */
    static void fnv_mix(uint32_t mix[], const uint32_t data[], size_t count);

    /**
     * @brief Calculate single DAG item
     * @param cache Pre-generated cache
     * @param index Item index in dataset
     * @return 64-byte DAG item
     */
    static hash64_t calculateDatasetItem(const std::vector<hash64_t>& cache, uint32_t index);

    /**
     * @brief Calculate full dataset size for given epoch
     */
    static uint64_t getDatasetSize(uint32_t epoch);

    /**
     * @brief Calculate cache size for given epoch
     */
    static uint64_t getCacheSize(uint32_t epoch);

    /**
     * @brief Get epoch number for given block
     */
    static uint32_t getEpoch(uint64_t blockNumber);

    /**
     * @brief Verify a mining solution
     */
    static bool verifySolution(
        const hash32_t& headerHash,
        uint64_t nonce,
        const hash32_t& mixHash,
        const hash32_t& result,
        uint64_t target
    );

private:
    static const uint32_t EPOCH_LENGTH = 30000;
    static const uint64_t MIX_BYTES = 128;
    static const uint32_t DATASET_PARENTS = 256;
    static const uint32_t CACHE_ROUNDS = 3;
    static const uint32_t FNV_PRIME = 0x01000193;
};

} // namespace ohmy
