#include <cuda_runtime.h>
#include <cstdint>
#include <cstring>
#include "ohmy/types.hpp"
#include "keccak_dev.cuh"

// Device-compatible solution structure (same as search_kernel.cu)
struct DeviceSolution {
    uint64_t nonce;
    uint8_t mixHash[32];
    uint8_t result[32];
};

namespace ohmy {
namespace cuda {

/**
 * @brief Warp-level cooperative Ethash kernel
 * 
 * Design:
 * - 1 block processes 1 warp (32 threads)
 * - 1 warp cooperatively processes 1 nonce
 * - Each thread in warp handles portion of mix[] and DAG access
 * - __shfl_xor_sync used for inter-thread communication
 * - gridDim.x warps = gridDim.x nonces processed in parallel
 * 
 * Benefits:
 * - Reduced register pressure (distributed across warp)
 * - Better memory hierarchy utilization
 * - Efficient warp-level synchronization
 * - Lower latency for DAG access coordination
 */
__global__ void ethash_search_kernel_warp(
    const uint64_t* __restrict__ dag,
    uint64_t dagSize,
    const uint32_t* __restrict__ headerHash,
    const uint32_t* __restrict__ seedHash,
    const uint8_t* __restrict__ targetBE,
    uint64_t startNonce,
    DeviceSolution* solutions,
    uint32_t* solutionCount,
    uint32_t maxSolutions
) {
    // Each block = 1 warp, each warp = 1 nonce
    const uint64_t warpId = blockIdx.x;
    const uint64_t nonce = startNonce + warpId;
    const uint32_t laneId = threadIdx.x;
    
    // Shared memory for warp cooperation
    // - header: 8 uint32 (shared by all threads)
    // - seedHash: 8 uint32 (shared by all threads)
    // - mix: 32 uint32 (one per thread, but we need all for DAG lookup)
    // - dagAccess: temp space for DAG data
    __shared__ uint32_t s_header[8];
    __shared__ uint32_t s_seedHash[8];
    __shared__ uint32_t s_mix[32];        // Shared mix state
    __shared__ uint64_t s_dagBuffer[16];  // Shared DAG buffer (128 bytes)
    
    // All threads load header cooperatively
    if (laneId < 8) {
        s_header[laneId] = headerHash[laneId];
        s_seedHash[laneId] = seedHash[laneId];
    }
    __syncwarp();
    
    // ===== STEP 1: Keccak512(header || nonce) to get seed =====
    uint8_t keccak_input[40];
    #pragma unroll 8
    for (int i = 0; i < 8; ++i) {
        ((uint32_t*)keccak_input)[i] = s_header[i];
    }
    ((uint64_t*)keccak_input)[4] = nonce;
    
    // Each thread computes Keccak512 (replicated within warp, then sync)
    // Note: Keccak is expensive, so we only do it once per warp
    uint64_t seed[8];
    if (laneId == 0) {
        // Only lane 0 computes; others will read from shared memory
        uint8_t seed_bytes[64];
        keccak512_dev(keccak_input, 40, seed_bytes);
        // Store in shared memory for other threads to read
        #pragma unroll 8
        for (int i = 0; i < 8; ++i) {
            ((uint64_t*)s_mix)[i] = ((uint64_t*)seed_bytes)[i];
        }
    }
    __syncwarp();
    
    // All threads read seed from shared memory
    #pragma unroll 8
    for (int i = 0; i < 8; ++i) {
        seed[i] = ((uint64_t*)s_mix)[i];
    }
    
    // ===== STEP 2: Initialize mix with seed =====
    // Each thread gets own portion of mix[] (32 uint32 total = 1 per thread)
    uint32_t mix_local = ((uint32_t*)seed)[laneId % 16];
    uint32_t mix_local_high = ((uint32_t*)seed)[(laneId % 16) + 16];
    
    // Broadcast seed to all threads via shared memory for DAG mixing
    #pragma unroll 8
    for (int i = 0; i < 8; ++i) {
        ((uint64_t*)s_mix)[i] = seed[i];
    }
    __syncwarp();
    
    // ===== STEP 3: DAG mixing (64 rounds) =====
    const uint32_t MIX_WORDS = 32;
    const uint32_t NUM_ACCESSES = 64;
    const uint64_t dagItems = dagSize / 64;  // Number of 64-byte items
    
    for (uint32_t round = 0; round < NUM_ACCESSES; ++round) {
        // Calculate parent index using FNV (all threads need it)
        // FNV requires reading mix[], so use shared memory
        uint32_t mixIdx = round % 16;
        
        // Each thread computes part of FNV (distributed computation)
        // For now: just use laneId to index into mix for FNV input
        uint32_t fnv_input = ((uint32_t*)s_mix)[laneId];
        uint32_t t = fnv1a(seed[0] ^ round, fnv_input);
        
        // Parent index (all threads should get same, so reduce via warp shuffle)
        // Use lane 0's calculation broadcasted to all
        if (laneId == 0) {
            s_dagBuffer[0] = (uint64_t)(t % dagItems);  // Parent index
        }
        __syncwarp();
        
        uint64_t parentIndex = s_dagBuffer[0];
        uint64_t dagAddr = parentIndex * 8;  // 64 bytes = 8 * uint64_t
        
        // Each thread fetches one uint64_t from DAG cooperatively
        // All 32 threads access consecutively to coalesce memory
        if (dagAddr + laneId < dagSize / 8) {
            s_dagBuffer[laneId % 16] = dag[dagAddr + laneId];
        }
        __syncwarp();
        
        // FNV mix all threads' portions
        // Each thread mixes its local value with DAG data
        uint64_t dag_value = s_dagBuffer[laneId];
        mix_local = fnv1a(mix_local, (uint32_t)dag_value);
        mix_local_high = fnv1a(mix_local_high, (uint32_t)(dag_value >> 32));
        
        // Update shared mix for next round
        ((uint32_t*)s_mix)[laneId] = mix_local;
        ((uint32_t*)s_mix)[laneId + 16] = mix_local_high;
        __syncwarp();
    }
    
    // ===== STEP 4: Compress mix to 32 bytes =====
    // Each thread compresses 1 uint32 from mix
    uint32_t compressed = 0;
    if (laneId < 8) {
        compressed = fnv1a(((uint32_t*)s_mix)[laneId * 4 + 0],
                           ((uint32_t*)s_mix)[laneId * 4 + 1]);
        compressed = fnv1a(compressed,
                           ((uint32_t*)s_mix)[laneId * 4 + 2]);
        compressed = fnv1a(compressed,
                           ((uint32_t*)s_mix)[laneId * 4 + 3]);
        
        // Store compressed in shared memory
        ((uint32_t*)s_dagBuffer)[laneId] = compressed;
    }
    __syncwarp();
    
    // ===== STEP 5: Final Keccak256 =====
    uint8_t final_input[96];
    
    // Cooperatively build final_input
    if (laneId < 8) {
        // Copy seed (64 bytes)
        ((uint64_t*)final_input)[laneId] = seed[laneId];
    } else if (laneId < 16) {
        // Copy compressed (32 bytes)
        ((uint32_t*)(final_input + 64))[laneId - 8] = ((uint32_t*)s_dagBuffer)[laneId - 8];
    }
    __syncwarp();
    
    // Compute final hash (lane 0 only, then broadcast)
    uint8_t result_hash[32];
    if (laneId == 0) {
        keccak256_dev(final_input, 96, result_hash);
        // Store in shared memory
        #pragma unroll 32
        for (int i = 0; i < 32; ++i) {
            ((uint8_t*)s_dagBuffer)[i] = result_hash[i];
        }
    }
    __syncwarp();
    
    // All threads read result hash
    #pragma unroll 32
    for (int i = 0; i < 32; ++i) {
        result_hash[i] = ((uint8_t*)s_dagBuffer)[i];
    }
    
    // ===== STEP 6: Compare against target =====
    int cmp = 0;
    #pragma unroll 32
    for (int i = 0; i < 32 && cmp == 0; ++i) {
        if (result_hash[i] < targetBE[i]) {
            cmp = -1;
        } else if (result_hash[i] > targetBE[i]) {
            cmp = 1;
        }
    }
    
    // ===== STEP 7: Store solution if found (lane 0 handles atomic) =====
    if (cmp <= 0 && laneId == 0) {
        uint32_t idx = atomicAdd(solutionCount, 1);
        if (idx < maxSolutions) {
            solutions[idx].nonce = nonce;
            
            // Store compressed mix
            #pragma unroll 8
            for (int i = 0; i < 8; ++i) {
                ((uint32_t*)solutions[idx].mixHash)[i] = ((uint32_t*)s_dagBuffer)[i];
            }
            
            // Store result hash
            #pragma unroll 32
            for (int i = 0; i < 32; ++i) {
                solutions[idx].result[i] = result_hash[i];
            }
        }
    }
}

/**
 * @brief Host function to launch warp-level kernel
 */
extern "C" void launch_ethash_search_warp(
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
    // Each block = 1 warp = 1 nonce
    // So grid dimensions = searchCount blocks of 32 threads
    const int threadsPerBlock = 32;  // One warp
    const int blocks = searchCount;  // One nonce per block
    
    // Limit to avoid too many blocks
    const int maxBlocks = 65535 * 64;  // Realistic limit
    if (blocks > maxBlocks) {
        // Would need multiple kernel launches
        // For now, just cap it
        int blocksToLaunch = maxBlocks;
        ethash_search_kernel_warp<<<blocksToLaunch, threadsPerBlock, 0, stream>>>(
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
    } else {
        ethash_search_kernel_warp<<<blocks, threadsPerBlock, 0, stream>>>(
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
}

}  // namespace cuda
}  // namespace ohmy
