#include "ohmy/ethash.hpp"
#include "ohmy/keccak.hpp"
#include <algorithm>
#include <cstring>

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

uint64_t Ethash::getDatasetSize(uint32_t epoch) {
    // TODO: Calculate exact dataset size for epoch
    // Base size: 1GB + (epoch * 8MB)
    const uint64_t baseSize = 1ULL << 30; // 1 GB
    const uint64_t growth = (8ULL << 20) * epoch; // 8 MB per epoch
    return baseSize + growth;
}

uint64_t Ethash::getCacheSize(uint32_t epoch) {
    // TODO: Calculate exact cache size for epoch
    // Base size: 16MB + (epoch * 128KB)
    const uint64_t baseSize = 16ULL << 20; // 16 MB
    const uint64_t growth = (128ULL << 10) * epoch; // 128 KB per epoch
    return baseSize + growth;
}

uint32_t Ethash::getEpoch(uint64_t blockNumber) {
    return static_cast<uint32_t>(blockNumber / EPOCH_LENGTH);
}

bool Ethash::verifySolution(
    const hash32_t& headerHash,
    uint64_t nonce,
    const hash32_t& mixHash,
    const hash32_t& result,
    uint64_t target
) {
    // TODO: Implement solution verification
    // 1. Reconstruct hash from header + nonce + mixHash
    // 2. Compare result with target
    return false;
}

} // namespace ohmy
