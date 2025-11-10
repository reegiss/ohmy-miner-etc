#pragma once

#include <cstdint>
#include <cuda_runtime.h>
#include "ohmy/cuda/core/device_solution.hpp"

extern "C" void launch_ethash_search(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    ohmy::cuda::DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);

extern "C" void launch_ethash_search_optimized(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    ohmy::cuda::DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);

extern "C" void launch_ethash_search_texture(
    cudaTextureObject_t texDAG,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    ohmy::cuda::DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);
