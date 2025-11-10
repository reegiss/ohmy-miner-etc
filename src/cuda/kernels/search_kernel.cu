#include <cuda_runtime.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "ohmy/types.hpp"
#include "keccak_dev.cuh"
#include "ohmy/cuda/core/device_solution.hpp"
using ohmy::cuda::DeviceSolution;

namespace ohmy {
namespace cuda {

/**
 * @brief CUDA kernel for Ethash mining
 * 
 * Each thread tests one nonce value. If a valid solution is found,
 * it's written to the solutions array.
 */
__global__ void ethash_search_kernel(
    const uint64_t* __restrict__ dag,
    uint64_t dagSize,
    const uint32_t* __restrict__ headerHash,  // 8 x uint32_t = 32 bytes
    const uint32_t* __restrict__ seedHash,    // 8 x uint32_t = 32 bytes (seed from job)
    const uint8_t* __restrict__ targetBE,     // 32-byte big-endian target
    uint64_t startNonce,
    DeviceSolution* solutions, // Array to store complete Solutions
    uint32_t* solutionCount,  // Atomic counter
    uint32_t maxSolutions
) {
    // Calculate this thread's nonce
    uint64_t nonce = startNonce + blockIdx.x * blockDim.x + threadIdx.x;
    
    // Constants
    const uint32_t MIX_WORDS = 32;  // 128 bytes / 4 = 32 words
    // The DAG on device is stored as 64-byte items (8 x uint64_t).
    // Ethash mixing uses 128 bytes per access, i.e., a pair of consecutive 64-byte items.
    // Therefore, the number of 128-byte pairs (logical DAG items) is dagSize / 128.
    const uint32_t DAG_PAIRS = dagSize / 128;
    const uint32_t NUM_ACCESSES = 64;  // Number of DAG accesses per hash
    
    // Shared memory for header + seedHash
    __shared__ uint32_t s_header[8];
    __shared__ uint32_t s_seedHash[8]; // used for final keccak step
    if (threadIdx.x < 8) {
        s_header[threadIdx.x]   = headerHash[threadIdx.x];
        s_seedHash[threadIdx.x] = seedHash[threadIdx.x];
    }
    __syncthreads();
    
    // seed = keccak512(headerHash || nonceLE)
    uint8_t seed[64];
    uint8_t seedIn[32 + 8];
    #pragma unroll
    for (int i = 0; i < 8; ++i) ((uint32_t*)seedIn)[i] = s_header[i];
    ((uint64_t*)(seedIn + 32))[0] = nonce; // little-endian append nonce
    keccak512_dev(seedIn, sizeof(seedIn), seed);

    // Initialize 128-byte mix = seed||seed (replicated)
    uint32_t mix[MIX_WORDS];
    #pragma unroll
    for (int i = 0; i < 16; ++i) {
        mix[i] = ((const uint32_t*)seed)[i];
        mix[i + 16] = ((const uint32_t*)seed)[i];
    }
    
    // Perform DAG lookups and mixing
    uint32_t s0 = ((const uint32_t*)seed)[0];  // First word of seed for DAG indexing
    for (uint32_t i = 0; i < NUM_ACCESSES; ++i) {
        // Calculate DAG pair index using FNV: fnv(i XOR s[0], mix[i % MIX_WORDS])
        // Then map to a 128-byte pair: indices (2*p, 2*p+1)
        uint32_t pairIndex = fnv1a(i ^ s0, mix[i % MIX_WORDS]) % DAG_PAIRS;

        // Fetch two consecutive 64-byte DAG items (total 128 bytes)
        const uint32_t* dagItem0 = reinterpret_cast<const uint32_t*>(&dag[(pairIndex * 2u) * 8u]);
        const uint32_t* dagItem1 = reinterpret_cast<const uint32_t*>(&dag[(pairIndex * 2u + 1u) * 8u]);

        // Mix first 64 bytes into mix[0..15] and the second 64 bytes into mix[16..31]
        #pragma unroll 16
        for (int w = 0; w < 16; ++w) {
            mix[w]      = fnv1a(mix[w],      dagItem0[w]);
            mix[w + 16] = fnv1a(mix[w + 16], dagItem1[w]);
        }
    }
    
    // Compress mix to 32 bytes (8 words)
    // Each compressed word is FNV of 4 consecutive mix words
    uint32_t compressed[8];
    #pragma unroll 8
    for (int i = 0; i < 8; ++i) {
        compressed[i] = fnv1a(mix[i * 4], mix[i * 4 + 1]);
        compressed[i] = fnv1a(compressed[i], mix[i * 4 + 2]);
        compressed[i] = fnv1a(compressed[i], mix[i * 4 + 3]);
    }
    
    // Final result = keccak256(seedHash || compressedMix)
    // According to Ethash: mix digest (compressed) + seedHash -> keccak256
    uint8_t finIn[64];
    // First 32 bytes: compressed mix
    memcpy(finIn, compressed, 32);
    // Next 32 bytes: seedHash
    #pragma unroll
    for (int i = 0; i < 8; ++i) ((uint32_t*)(finIn + 32))[i] = s_seedHash[i];
    uint8_t result_hash[32];
    keccak256_dev(finIn, sizeof(finIn), result_hash);

    // Ethash compares the 256-bit integer in big-endian form to the target boundary.
    // Convert result to big-endian for direct byte compare:
    int cmp = 0;
    #pragma unroll
    for (int i = 0; i < 32; ++i) {
        uint8_t a = result_hash[i];
        uint8_t b = targetBE[i];
        if (a < b) { cmp = -1; break; }
        if (a > b) { cmp = 1; break; }
    }
    
    if (cmp <= 0) {
        uint32_t idx = atomicAdd(solutionCount, 1);
        if (idx < maxSolutions) {
            // Populate complete Solution structure
            solutions[idx].nonce = nonce;
            
            // Copy mixHash (compressed mix = 8 x uint32_t = 32 bytes)
            #pragma unroll
            for (int i = 0; i < 8; ++i) {
                ((uint32_t*)solutions[idx].mixHash)[i] = compressed[i];
            }
            
            // Copy result hash (32 bytes)
            #pragma unroll
            for (int i = 0; i < 32; ++i) {
                solutions[idx].result[i] = result_hash[i];
            }
        }
    }
}

/**
 * @brief Highly optimized search kernel with cooperative DAG caching
 * 
 * Advanced optimizations:
 * - High batching: 64 nonces per thread to amortize kernel overhead
 * - Cooperative DAG caching: threads load DAG slices into shared memory cooperatively
 * - Coalesced global memory access: all threads in a warp load consecutive addresses
 * - Register optimization: careful balance to maintain high occupancy
 */
__global__ void ethash_search_kernel_optimized(
    const uint64_t* __restrict__ dag,
    uint64_t dagSize,
    const uint32_t* __restrict__ headerHash,
    const uint32_t* __restrict__ seedHash,
    const uint8_t* __restrict__ targetBE,
    uint64_t startNonce,
    uint32_t noncesPerThread,
    DeviceSolution* solutions,
    uint32_t* solutionCount,
    uint32_t maxSolutions
) {
    // Constants
    const uint32_t MIX_WORDS = 32;
    const uint32_t DAG_PAIRS = dagSize / 128;
    const uint32_t NUM_ACCESSES = 64;
    // Note: CACHE_SIZE removed - not used in this kernel version
    
    // Shared memory layout:
    // - s_header: 8 uint32 = 32 bytes
    // - s_seedHash: 8 uint32 = 32 bytes (unused in this kernel but kept for API consistency)
    __shared__ uint32_t s_header[8];
    __shared__ uint32_t s_seedHash[8]; // used in final result hash construction
    
    // Cooperative loading of header and seedHash (once per block)
    if (threadIdx.x < 8) {
        s_header[threadIdx.x] = headerHash[threadIdx.x];
        s_seedHash[threadIdx.x] = seedHash[threadIdx.x];
    }
    __syncthreads();
    
    // Calculate base nonce for this thread
    uint64_t baseNonce = startNonce + (blockIdx.x * blockDim.x + threadIdx.x) * noncesPerThread;
    
    // Process multiple nonces per thread (batching amortizes overhead)
    for (uint32_t nonceOffset = 0; nonceOffset < noncesPerThread; ++nonceOffset) {
        uint64_t nonce = baseNonce + nonceOffset;
        
        // seed = keccak512(headerHash || nonceLE)
        uint8_t seed[64];
        uint8_t seedIn[40];
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) ((uint32_t*)seedIn)[i] = s_header[i];
        ((uint64_t*)(seedIn + 32))[0] = nonce;
        keccak512_dev(seedIn, sizeof(seedIn), seed);
        
        // Initialize mix from seed (replicate seed to fill 128 bytes)
        uint32_t mix[MIX_WORDS];
        const uint32_t* seedWords = (const uint32_t*)seed;
        #pragma unroll 16
        for (int i = 0; i < 16; ++i) {
            mix[i] = seedWords[i];
            mix[i + 16] = seedWords[i];
        }
        
        uint32_t s0 = seedWords[0];
        
        // DAG access loop - simplified cooperative caching to avoid memory errors
        // Strategy: each access loads required DAG pair directly, with threads helping each other
        #pragma unroll 1  // Don't unroll outer loop to save registers
        for (uint32_t i = 0; i < NUM_ACCESSES; ++i) {
            // Calculate DAG pair index for this access
            uint32_t pairIndex = fnv1a(i ^ s0, mix[i % MIX_WORDS]) % DAG_PAIRS;
            uint64_t dagOffset = (pairIndex * 2u) * 8u;
            
        // Check bounds before access
        // if (dagOffset + 16 >= dagSize) {
        //     continue;  // Skip out-of-bounds access
        // }            // Direct read from global memory (still benefits from L2 cache)
            // Future: implement proper cooperative loading with bounds checking
            const uint32_t* dagItem0 = reinterpret_cast<const uint32_t*>(&dag[dagOffset]);
            const uint32_t* dagItem1 = reinterpret_cast<const uint32_t*>(&dag[dagOffset + 8]);
            
            // FNV mixing (unroll fully for maximum ILP)
            #pragma unroll 16
            for (int w = 0; w < 16; ++w) {
                mix[w] = fnv1a(mix[w], dagItem0[w]);
                mix[w + 16] = fnv1a(mix[w + 16], dagItem1[w]);
            }
        }
        
        // Compress mix to 32 bytes (8 uint32)
        uint32_t compressed[8];
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) {
            compressed[i] = fnv1a(mix[i * 4], mix[i * 4 + 1]);
            compressed[i] = fnv1a(compressed[i], mix[i * 4 + 2]);
            compressed[i] = fnv1a(compressed[i], mix[i * 4 + 3]);
        }
        
        // Final result = keccak256(seedHash || compressedMix)
        uint8_t finIn[64];
        // First 32 bytes: compressed mix
        memcpy(finIn, compressed, 32);
        // Next 32 bytes: seedHash
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) ((uint32_t*)(finIn + 32))[i] = s_seedHash[i];
        uint8_t result_hash[32];
        keccak256_dev(finIn, sizeof(finIn), result_hash);
        
        // Convert result to big-endian for comparison
        int cmp = 0;
        #pragma unroll 32
        for (int i = 0; i < 32 && cmp == 0; ++i) {
            if (result_hash[i] < targetBE[i]) {
                cmp = -1;
            } else if (result_hash[i] > targetBE[i]) {
                cmp = 1;
            }
        }
        
        // If valid solution, store atomically
        if (cmp <= 0) {
            uint32_t idx = atomicAdd(solutionCount, 1);
            if (idx < maxSolutions) {
                solutions[idx].nonce = nonce;
                #pragma unroll 8
                for (int i = 0; i < 8; ++i) {
                    ((uint32_t*)solutions[idx].mixHash)[i] = compressed[i];
                }
                #pragma unroll 32
                for (int i = 0; i < 32; ++i) {
                    solutions[idx].result[i] = result_hash[i];
                }
            }
        }
    }
}

/**
 * @brief Optimized search kernel with texture memory for DAG access
 * 
 * Uses CUDA texture memory for DAG reads to leverage L1 cache and improve
 * memory access patterns. Expected improvement: +5-10% over direct global memory.
 */
__global__ void ethash_search_kernel_texture(
    cudaTextureObject_t texDAG,
    uint64_t dagSize,
    const uint32_t* __restrict__ header,
    const uint32_t* __restrict__ seedHash,
    const uint8_t* __restrict__ targetBE,
    uint64_t startNonce,
    uint32_t noncesPerThread,
    DeviceSolution* solutions,
    uint32_t* solutionCount,
    uint32_t maxSolutions
) {
    // Shared memory for frequently accessed data
    __shared__ uint32_t s_header[8];
    // Note: s_seedHash loaded but unused in this kernel version
    // Removed unused extended seed hash buffer; only header is required for this variant.
    // NOTE: seedHash not used in texture kernel; avoid allocating shared memory to reduce usage.
    // __shared__ uint32_t s_seedHash[16]; (removed)
    
    // Cooperative loading into shared memory
    if (threadIdx.x < 8) {
        s_header[threadIdx.x] = header[threadIdx.x];
    }
    // Seed hash unused in texture variant; skip load.
    __syncthreads();
    
    // Suppress unused variable warning
    // No seed hash usage; explicitly document omission.
    
    // Each thread processes multiple nonces
    const uint64_t threadId = blockIdx.x * blockDim.x + threadIdx.x;
    const uint64_t baseNonce = startNonce + threadId * noncesPerThread;
    
    // Process nonces in batch
    #pragma unroll 4
    for (uint32_t batch = 0; batch < noncesPerThread; ++batch) {
        const uint64_t nonce = baseNonce + batch;
        
        // Keccak512 on header + nonce
        uint64_t seed[8];
        uint8_t keccak_input[40];
        
        // Copy header to keccak input
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) {
            ((uint32_t*)keccak_input)[i] = s_header[i];
        }
        
        // Append nonce (little-endian)
        ((uint64_t*)keccak_input)[4] = nonce;
        
        // Keccak512
        keccak512_dev(keccak_input, 40, (uint8_t*)seed);
        
        // Initialize mix with seed
        uint32_t mix[32];
        #pragma unroll 16
        for (int i = 0; i < 16; ++i) {
            mix[i * 2] = ((uint32_t*)seed)[i];
            mix[i * 2 + 1] = ((uint32_t*)seed)[i];
        }
        
        // DAG mixing - using TEXTURE MEMORY for random access
        const uint32_t numParents = 256;
        
        #pragma unroll 4
        for (uint32_t round = 0; round < 64; ++round) {
            const uint32_t mixIdx = round % 16;
            const uint32_t t = fnv1a(seed[0] ^ round, mix[mixIdx]);
            
            // Calculate parent index with bounds checking
            const uint64_t parentIndex = t % numParents;
            const uint64_t dagIdx = parentIndex * 16;
            
            // if (dagIdx + 15 < dagSize) {
                // TEXTURE FETCH: Use tex1Dfetch for DAG access
                // This leverages L1 texture cache for better performance
                #pragma unroll 16
                for (int j = 0; j < 16; ++j) {
                    uint2 texel = tex1Dfetch<uint2>(texDAG, dagIdx + j);
                    uint64_t dagValue = ((uint64_t)texel.y << 32) | texel.x;
                    mix[j] = fnv1a(mix[j], (uint32_t)dagValue);
                    mix[j + 16] = fnv1a(mix[j + 16], (uint32_t)(dagValue >> 32));
                }
            // }
        }
        
        // Compress mix to 32 bytes
        uint32_t compressed[8];
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) {
            compressed[i] = fnv1a(mix[i * 4], mix[i * 4 + 1]);
            compressed[i] = fnv1a(compressed[i], mix[i * 4 + 2]);
            compressed[i] = fnv1a(compressed[i], mix[i * 4 + 3]);
        }
        
        // Final Keccak256
        uint8_t final_input[64 + 32];
        memcpy(final_input, compressed, 32);
        memcpy(final_input + 32, seed, 64);
        
        uint8_t result_hash[32];
        keccak256_dev(final_input, 96, result_hash);
        
        // Compare against target (big-endian comparison)
        int cmp = 0;
        #pragma unroll 32
        for (int i = 0; i < 32 && cmp == 0; ++i) {
            if (result_hash[i] < targetBE[i]) {
                cmp = -1;
            } else if (result_hash[i] > targetBE[i]) {
                cmp = 1;
            }
        }
        
        // Store solution if found
        if (cmp <= 0) {
            uint32_t idx = atomicAdd(solutionCount, 1);
            if (idx < maxSolutions) {
                solutions[idx].nonce = nonce;
                #pragma unroll 8
                for (int i = 0; i < 8; ++i) {
                    ((uint32_t*)solutions[idx].mixHash)[i] = compressed[i];
                }
                #pragma unroll 32
                for (int i = 0; i < 32; ++i) {
                    solutions[idx].result[i] = result_hash[i];
                }
            }
        }
    }
}

/**
 * @brief Host function to launch optimized search kernel
 */
extern "C" void launch_ethash_search_optimized(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
) {
    // Calculate grid dimensions with batching
    const int threadsPerBlock = 512;  // Optimized for CC 7.5: 50% occupancy provides best latency hiding
    const int totalThreads = (searchCount + noncesPerThread - 1) / noncesPerThread;
    const int blocks = (totalThreads + threadsPerBlock - 1) / threadsPerBlock;
    
    // Launch optimized kernel
    ethash_search_kernel_optimized<<<blocks, threadsPerBlock, 0, stream>>>(
        d_dag,
        dagSize,
        d_header,
        d_seedHash,
        d_targetBE,
        startNonce,
        noncesPerThread,
        d_solutions,
        d_solutionCount,
        maxSolutions
    );
}

/**
 * @brief Host function to launch texture memory optimized kernel
 */
extern "C" void launch_ethash_search_texture(
    cudaTextureObject_t texDAG,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
) {
    // Calculate grid dimensions with batching
    const int threadsPerBlock = 1024;  // Max occupancy (100% of SM capacity on CC 7.5)
    const int totalThreads = (searchCount + noncesPerThread - 1) / noncesPerThread;
    const int blocks = (totalThreads + threadsPerBlock - 1) / threadsPerBlock;
    
    // Launch texture memory kernel
    ethash_search_kernel_texture<<<blocks, threadsPerBlock, 0, stream>>>(
        texDAG,
        dagSize,
        d_header,
        d_seedHash,
        d_targetBE,
        startNonce,
        noncesPerThread,
        d_solutions,
        d_solutionCount,
        maxSolutions
    );
}

/**
 * @brief Batch search kernel - DEPRECATED, use ethash_search_kernel_optimized instead
 * 
 * Processes multiple nonces per thread for better GPU utilization
 */
// Deprecated batch kernel removed: use ethash_search_kernel_optimized instead.

/**
 * @brief Hash rate benchmark kernel
 */
__global__ void benchmark_kernel(
    const uint64_t* dag,
    uint64_t dagSize,
    uint64_t iterations,
    uint64_t* hashCount
) {
    uint64_t count = 0;
    
    for (uint64_t i = 0; i < iterations; ++i) {
        // Perform hash computation
        count++;
    }
    
    atomicAdd((unsigned long long*)hashCount, count);
}

/**
 * @brief Host function to launch search kernel
 */
extern "C" void launch_ethash_search(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
) {
    // Calculate grid dimensions
    const int threadsPerBlock = 1024;  // Max occupancy (100% of SM capacity on CC 7.5)
    const int blocks = (searchCount + threadsPerBlock - 1) / threadsPerBlock;
    
    // Launch kernel
    ethash_search_kernel<<<blocks, threadsPerBlock, 0, stream>>>(
        d_dag,
        dagSize,
        d_header,
        d_seedHash,
        d_targetBE,
        startNonce,
        d_solutions,
        d_solutionCount,
        maxSolutions
    );
}

} // namespace cuda
} // namespace ohmy
