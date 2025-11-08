#include "ohmy/types.hpp"
#include <cuda_runtime.h>
#include <cstdio>

// CUDA error checking macro
#define CUDA_CHECK(call) do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
    } \
} while(0)

namespace ohmy {
namespace cuda {

// FNV1a hash constant
__device__ __forceinline__ uint32_t fnv1a_device(uint32_t hash, uint32_t value) {
    return (hash ^ value) * 0x01000193u;
}

// Keccak-f[1600] permutation for device
__device__ void keccak_f1600_dag(uint64_t state[25]) {
    const uint64_t keccakf_rndc[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
        0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
        0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
        0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
        0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
        0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
    };
    
    const int keccakf_rotc[24] = {
        1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
        27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
    };
    
    const int keccakf_piln[24] = {
        10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
    };
    
    for (int round = 0; round < 24; round++) {
        uint64_t C[5], D[5];
        
        // Theta
        for (int x = 0; x < 5; x++) {
            C[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (int x = 0; x < 5; x++) {
            D[x] = C[(x + 4) % 5] ^ ((C[(x + 1) % 5] << 1) | (C[(x + 1) % 5] >> 63));
        }
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                state[y * 5 + x] ^= D[x];
            }
        }
        
        // Rho and Pi
        uint64_t t = state[1];
        for (int i = 0; i < 24; i++) {
            int j = keccakf_piln[i];
            C[0] = state[j];
            state[j] = (t << keccakf_rotc[i]) | (t >> (64 - keccakf_rotc[i]));
            t = C[0];
        }
        
        // Chi
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 5; x++) {
                C[x] = state[y * 5 + x];
            }
            for (int x = 0; x < 5; x++) {
                state[y * 5 + x] ^= (~C[(x + 1) % 5]) & C[(x + 2) % 5];
            }
        }
        
        // Iota
        state[0] ^= keccakf_rndc[round];
    }
}

// Keccak-512 device function
__device__ void keccak512_device(uint8_t* data, size_t len, uint8_t out[64]) {
    uint64_t state[25] = {0};
    
    // Absorb
    const size_t rate = 72; // 1600 - 2*512 = 576 bits = 72 bytes
    size_t blockSize = 0;
    
    for (size_t i = 0; i < len; i++) {
        state[blockSize / 8] ^= static_cast<uint64_t>(data[i]) << (8 * (blockSize % 8));
        blockSize++;
        
        if (blockSize == rate) {
            keccak_f1600_dag(state);
            blockSize = 0;
        }
    }
    
    // Padding
    state[blockSize / 8] ^= 0x01ULL << (8 * (blockSize % 8));
    state[(rate - 1) / 8] ^= 0x80ULL << (8 * ((rate - 1) % 8));
    keccak_f1600_dag(state);
    
    // Squeeze
    for (int i = 0; i < 64; i++) {
        out[i] = static_cast<uint8_t>(state[i / 8] >> (8 * (i % 8)));
    }
}

// Calculate single DAG item on GPU
__global__ void generate_dag_item_kernel(
    const uint64_t* cache,      // Cache data (read-only)
    uint64_t* dag,              // Output DAG
    uint32_t numCacheItems,     // Number of cache items
    uint32_t startIndex,        // Starting DAG index
    uint32_t numItems           // Number of items to generate
) {
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= numItems) return;
    
    uint32_t dagIndex = startIndex + idx;
    
    const uint32_t HASH_BYTES = 64;
    const uint32_t HASH_WORDS = HASH_BYTES / 4;  // 16 words
    const uint32_t DATASET_PARENTS = 256;
    
    // Initialize mix with cache item
    uint8_t mix[64];
    uint32_t cacheIndex = dagIndex % numCacheItems;
    
    // Copy cache item to mix
    const uint64_t* cacheItem = &cache[cacheIndex * 8]; // 8 uint64_t per hash64_t
    uint64_t* mixData = reinterpret_cast<uint64_t*>(mix);
    for (int i = 0; i < 8; i++) {
        mixData[i] = cacheItem[i];
    }
    
    // Cast to uint32_t for word-level operations
    uint32_t* mixWords = reinterpret_cast<uint32_t*>(mix);
    
    // XOR first word with index
    mixWords[0] ^= dagIndex;
    
    // Initial hash
    uint8_t tempOut[64];
    keccak512_device(mix, 64, tempOut);
    for (int i = 0; i < 64; i++) {
        mix[i] = tempOut[i];
    }
    
    // Mix in DATASET_PARENTS cache items
    for (uint32_t i = 0; i < DATASET_PARENTS; ++i) {
        // Determine parent index
        uint32_t parentIndex = fnv1a_device(dagIndex ^ i, mixWords[i % HASH_WORDS]) % numCacheItems;
        
        // Get parent data
        const uint64_t* parentData = &cache[parentIndex * 8];
        
        // FNV mix all words
        for (uint32_t w = 0; w < HASH_WORDS; ++w) {
            uint32_t parentWord = (w < 8) ? 
                static_cast<uint32_t>(parentData[w / 2] >> ((w % 2) * 32)) :
                static_cast<uint32_t>(parentData[w / 2] >> ((w % 2) * 32));
            mixWords[w] = fnv1a_device(mixWords[w], parentWord);
        }
    }
    
    // Final hash
    keccak512_device(mix, 64, tempOut);
    for (int i = 0; i < 64; i++) {
        mix[i] = tempOut[i];
    }
    
    // Write result to DAG
    uint64_t* dagItem = &dag[dagIndex * 8];
    for (int i = 0; i < 8; i++) {
        dagItem[i] = mixData[i];
    }
}

// Host function to generate DAG on GPU
void generateDagGpu(
    const void* d_cache,        // Device cache pointer
    void* d_dag,                // Device DAG pointer
    uint32_t numCacheItems,     // Number of cache items
    uint32_t numDagItems        // Number of DAG items to generate
) {
    const int threadsPerBlock = 256;
    const int numBlocks = (numDagItems + threadsPerBlock - 1) / threadsPerBlock;
    
    // Process in batches to avoid long kernel execution
    const uint32_t batchSize = 1024 * 1024; // 1M items per batch
    
    for (uint32_t start = 0; start < numDagItems; start += batchSize) {
        uint32_t count = std::min(batchSize, numDagItems - start);
        int blocks = (count + threadsPerBlock - 1) / threadsPerBlock;
        
        generate_dag_item_kernel<<<blocks, threadsPerBlock>>>(
            static_cast<const uint64_t*>(d_cache),
            static_cast<uint64_t*>(d_dag),
            numCacheItems,
            start,
            count
        );
        
        // Check for errors
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
    }
}

} // namespace cuda
} // namespace ohmy
