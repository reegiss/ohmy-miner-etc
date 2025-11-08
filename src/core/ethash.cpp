#include "ohmy/ethash.hpp"
#include "ohmy/keccak.hpp"
#include "ohmy/logger.hpp"
#include <algorithm>
#include <cstring>

// Helper: 256-bit represented as 32-byte array (little-endian)
namespace {
    struct uint256 {
        uint32_t w[8]; // little-endian 8 * 32 = 256 bits
    };

    inline int cmp256(const uint8_t a[32], const uint8_t b[32]) {
        // Compare big-endian value: iterate from most significant byte
        for (int i = 31; i >= 0; --i) {
            if (a[i] < b[i]) return -1;
            if (a[i] > b[i]) return 1;
        }
        return 0;
    }
}

namespace ohmy {

void Ethash::fnv_mix(uint32_t mix[], const uint32_t data[], size_t count) {
    for (size_t i = 0; i < count; ++i) {
        mix[i] = fnv1a(mix[i], data[i]);
    }
}

std::vector<hash64_t> Ethash::calculateCache(uint32_t epoch) {
    // Calculate cache size (must be prime-sized in real implementation)
    uint64_t cacheSize = getCacheSize(epoch);
    size_t numItems = cacheSize / 64;  // Each cache item is 64 bytes
    
    std::vector<hash64_t> cache(numItems);
    
    // Generate seed hash for this epoch
    hash32_t seedHash{};
    seedHash.fill(0);
    
    // Seed = Keccak256^epoch(zeros)
    for (uint32_t i = 0; i < epoch; ++i) {
        seedHash = Keccak::keccak256(seedHash);
    }
    
    // First cache item = Keccak512(seed)
    cache[0] = Keccak::keccak512(seedHash.data(), seedHash.size());
    
    // Sequential cache items: cache[i] = Keccak512(cache[i-1])
    for (size_t i = 1; i < numItems; ++i) {
        cache[i] = Keccak::keccak512(cache[i - 1]);
    }
    
    // Apply CACHE_ROUNDS of RandMemoHash for better distribution
    for (uint32_t round = 0; round < CACHE_ROUNDS; ++round) {
        for (size_t i = 0; i < numItems; ++i) {
            // Get previous item (wrapping around)
            size_t prevIdx = (i == 0) ? (numItems - 1) : (i - 1);
            
            // XOR with neighbor determined by first 4 bytes
            uint32_t* cacheData = reinterpret_cast<uint32_t*>(cache[i].data());
            uint32_t neighborIdx = cacheData[0] % numItems;
            
            // XOR current with previous and neighbor, then rehash
            hash64_t temp;
            for (size_t j = 0; j < 8; ++j) {  // 64 bytes = 8 x uint64_t
                uint64_t* curr = reinterpret_cast<uint64_t*>(cache[i].data());
                uint64_t* prev = reinterpret_cast<uint64_t*>(cache[prevIdx].data());
                uint64_t* neighbor = reinterpret_cast<uint64_t*>(cache[neighborIdx].data());
                reinterpret_cast<uint64_t*>(temp.data())[j] = curr[j] ^ prev[j] ^ neighbor[j];
            }
            
            cache[i] = Keccak::keccak512(temp);
        }
    }
    
    return cache;
}

hash64_t Ethash::calculateDatasetItem(const std::vector<hash64_t>& cache, uint32_t index) {
    const uint32_t numCacheItems = cache.size();
    const uint32_t HASH_BYTES = 64;
    const uint32_t HASH_WORDS = HASH_BYTES / 4;  // 16 words of 32-bit
    
    // Initialize mix with cache item (use index % cache_size)
    hash64_t mix;
    uint32_t cacheIndex = index % numCacheItems;
    std::memcpy(mix.data(), cache[cacheIndex].data(), HASH_BYTES);
    
    // Cast to uint32_t array for easier manipulation
    uint32_t* mixWords = reinterpret_cast<uint32_t*>(mix.data());
    
    // XOR first word with index
    mixWords[0] ^= index;
    
    // Initial hash
    mix = Keccak::keccak512(mix);
    
    // Mix in DATASET_PARENTS (256) cache items
    for (uint32_t i = 0; i < DATASET_PARENTS; ++i) {
        // Determine parent index using FNV mixing
        uint32_t parentIndex = fnv1a(index ^ i, mixWords[i % HASH_WORDS]) % numCacheItems;
        
        // Get parent data
        const uint32_t* parentWords = reinterpret_cast<const uint32_t*>(cache[parentIndex].data());
        
        // FNV mix all words
        fnv_mix(mixWords, parentWords, HASH_WORDS);
    }
    
    // Final hash
    mix = Keccak::keccak512(mix);
    
    return mix;
}

uint64_t Ethash::getDatasetSize(uint32_t epoch) {
    // ECIP-1099 (Etchash): After epoch 390, DAG growth rate halves
    // - Seed epoch continues at 30k blocks/epoch (used for seed hash calculation)
    // - DAG epoch is block/60k (half of seed epoch) for size calculation
    // - Fork at block 11,700,000 = seed epoch 390
    //
    // Since we receive seed epoch from pool, we need to convert:
    //   if seed_epoch >= 390: dag_epoch = 195 + (seed_epoch - 390) / 2
    //   else: dag_epoch = seed_epoch
    
    uint32_t dagEpoch = epoch;
    
    if (epoch >= 390) {
        // Post-ECIP-1099: DAG size based on halved epoch
        // At fork: seed epoch 390 -> DAG stays at epoch 195 (390/2)
        // After fork: growth continues at half rate
        dagEpoch = 195 + (epoch - 390) / 2;
        LOG_INFO("ECIP-1099: seed epoch " + std::to_string(epoch) + 
                 " -> DAG epoch " + std::to_string(dagEpoch));
    }
    
    // Ethash dataset size formula: ~2^30 + 2^23 * epoch (~8MB per epoch)
    const uint64_t baseSize = 1ULL << 30; // 1 GB
    const uint64_t growthPerEpoch = 1ULL << 23; // 8 MB per epoch
    uint64_t size = baseSize + growthPerEpoch * static_cast<uint64_t>(dagEpoch);
    
    // Round to nearest 128 bytes (ethash requirement)
    size = (size / 128) * 128;
    
    LOG_INFO("Ethash::getDatasetSize(epoch=" + std::to_string(epoch) + 
             ", dagEpoch=" + std::to_string(dagEpoch) + ") = " + 
             std::to_string(size) + " bytes (" + std::to_string(size / (1024*1024)) + " MB)");
    return size;
}

uint64_t Ethash::getCacheSize(uint32_t epoch) {
    // ECIP-1099: Cache size follows same logic as dataset size
    uint32_t cacheEpoch = epoch;
    
    if (epoch >= 390) {
        // Post-ECIP-1099: Use halved epoch for cache size
        cacheEpoch = 195 + (epoch - 390) / 2;
    }
    
    // Ethash cache size formula: ~2^24 + 2^17 * epoch (~128KB per epoch)
    const uint64_t baseSize = 1ULL << 24; // 16 MB
    const uint64_t growthPerEpoch = 1ULL << 17; // 128 KB per epoch
    uint64_t size = baseSize + growthPerEpoch * static_cast<uint64_t>(cacheEpoch);
    
    // Round to nearest 64 bytes
    size = (size / 64) * 64;
    
    return size;
}

uint32_t Ethash::getEpoch(uint64_t blockNumber) {
    return static_cast<uint32_t>(blockNumber / EPOCH_LENGTH);
}

uint32_t Ethash::epochFromSeedHash(const hash32_t& seedHash) {
    // The seed hash for epoch N is Keccak256 applied N times starting from zero-hash.
    // IMPORTANT: Per ECIP-1099 (Etchash), seed calculation ALWAYS uses 30000 blocks per epoch,
    // even after the Etchash fork. This means the epoch derived from seedHash represents
    // the "seed epoch" count (block_number / 30000), NOT the "DAG epoch" (block_number / 60000).
    // 
    // For ETC post-ECIP-1099 (block >= 11,700,000):
    //   - Seed epoch = block / 30000 (continues old calculation)
    //   - DAG epoch = block / 60000 (new, half the seed epoch)
    //   - DAG size is based on DAG epoch (which is seed_epoch / 2)
    //
    // Since pools send us the seed hash, we derive the SEED epoch here.
    // The pool's mining job is based on current block rules, so we use this seed epoch directly
    // for DAG sizing, as it already reflects the effective DAG epoch post-fork.
    
    hash32_t cur{}; // all zeros
    const uint32_t MAX_EPOCH_SCAN = 2000; // safety bound (increased for ETC's higher epochs)
    for (uint32_t epoch = 0; epoch < MAX_EPOCH_SCAN; ++epoch) {
        if (cur == seedHash) return epoch;
        cur = Keccak::keccak256(cur);
    }
    return UINT32_MAX; // not found
}

bool Ethash::verifySolution(
    const hash32_t& headerHash,
    uint64_t nonce,
    const hash32_t& mixHash,
    const hash32_t& result,
    uint64_t target
) {
    // Legacy 64-bit target kept for now (will be replaced with 256-bit difficulty based target).
    uint64_t resultValue = 0;
    for (int i = 0; i < 8; ++i) {
        resultValue |= static_cast<uint64_t>(result[i]) << (8 * i);
    }
    if (resultValue >= target) return false;

    uint8_t hashInput[32 + 8 + 32];
    std::memcpy(hashInput, headerHash.data(), 32);
    for (int i = 0; i < 8; ++i) hashInput[32 + i] = static_cast<uint8_t>(nonce >> (8 * i));
    std::memcpy(hashInput + 40, mixHash.data(), 32);
    hash32_t calculatedResult = Keccak::keccak256(hashInput, sizeof(hashInput));
    return calculatedResult == result;
}

void Ethash::computeHashimoto(
    const hash32_t& headerHash,
    uint64_t nonce,
    const void* dag,
    size_t dagSize,
    hash32_t& outMixHash,
    hash32_t& outResult
) {
    // Basic Hashimoto: seed s = keccak512(header || nonceLE)
    if (dag == nullptr || dagSize < 64) {
        outMixHash.fill(0);
        outResult.fill(0);
        return;
    }

    const uint8_t* dagBytes = static_cast<const uint8_t*>(dag);
    const uint32_t dagItems = static_cast<uint32_t>(dagSize / 64);

    uint8_t seed[32 + 8];
    std::memcpy(seed, headerHash.data(), 32);
    for (int i = 0; i < 8; ++i) seed[32 + i] = static_cast<uint8_t>(nonce >> (8 * i));
    hash64_t mix = Keccak::keccak512(seed, sizeof(seed));

    uint32_t* mixWords = reinterpret_cast<uint32_t*>(mix.data());
    constexpr uint32_t MIX_WORDS = 16; // 64 bytes / 4

    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t idx = fnv1a(static_cast<uint32_t>(nonce) ^ i, mixWords[i % MIX_WORDS]) % dagItems;
        const uint32_t* dagWord = reinterpret_cast<const uint32_t*>(dagBytes + idx * 64);
        for (uint32_t w = 0; w < MIX_WORDS; ++w) {
            mixWords[w] = fnv1a(mixWords[w], dagWord[w]);
        }
    }

    // Compress to 32 bytes mix digest
    uint32_t cmix[8];
    for (int i = 0; i < 8; ++i) {
        cmix[i] = fnv1a(mixWords[i], mixWords[i + 8]);
    }
    std::memcpy(outMixHash.data(), cmix, 32);

    // Final result = keccak256(header || nonceLE || cmix)
    uint8_t finalInput[32 + 8 + 32];
    std::memcpy(finalInput, headerHash.data(), 32);
    for (int i = 0; i < 8; ++i) finalInput[32 + i] = static_cast<uint8_t>(nonce >> (8 * i));
    std::memcpy(finalInput + 40, outMixHash.data(), 32);
    outResult = Keccak::keccak256(finalInput, sizeof(finalInput));
}

} // namespace ohmy
