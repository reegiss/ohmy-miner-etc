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
     * @brief Calculate light cache for given epoch
     */
    static std::vector<hash64_t> calculateCache(uint32_t epoch);

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
};

} // namespace ohmy
