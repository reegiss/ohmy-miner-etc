#include "ohmy/types.hpp"
#include <stdint.h>
#include <stdint.h>
#pragma once

#include <cstdint>
#include <vector>
#include <cuda_runtime.h>
#include "ohmy/types.hpp"
#include "ohmy/cuda/core/device_solution.hpp"
#include "ohmy/cuda/kernels/kernel_launcher.hpp"
#include "ohmy/cuda/pipeline/pipeline_manager.hpp"
#include "ohmy/cuda/utils/cuda_check.hpp"
#include "ohmy/logger.hpp"

namespace ohmy { namespace cuda {

// Light value object describing a work submission
struct SearchWork {
    const uint64_t* d_dag = nullptr;
    uint64_t dagSize = 0;
    const hash32_t* headerHash = nullptr;
    const hash32_t* seedHash = nullptr;
    const uint8_t* targetBE = nullptr; // host pointer to 32 bytes
    uint64_t startNonce = 0;
    uint64_t count = 0;
    std::string jobId;
    uint32_t epoch = 0;
};

// Extracted async + sync search orchestration.
// Owns no memory; relies on PipelineManager and external device pointers.
class SearchExecutor {
public:
    SearchExecutor(PipelineManager* pipeline) : pipeline_(pipeline) {}

    // Synchronous single-batch fallback (used only if legacy path still required)
    uint32_t runSync(const SearchWork& work,
                     const uint32_t maxSolutions,
                     const uint32_t* d_header,
                     const uint32_t* d_seedHash,
                     const uint8_t* d_target,
                     DeviceSolution* d_solutions,
                     uint32_t* d_solutionCount,
                     std::vector<Solution>& outSolutions,
                     cudaStream_t streamCompute,
                     cudaStream_t streamMemory,
                     cudaStream_t streamIO,
                     cudaEvent_t startEvent,
                     cudaEvent_t stopEvent,
                     cudaEvent_t memoryDone,
                     cudaEvent_t kernelDone,
                     cudaEvent_t resultsDone);

    // Tick-based async pipeline execution. Returns solutions from the completed stream.
    uint32_t runAsyncTick(const SearchWork& newWork,
                          const uint32_t maxSolutions,
                          std::vector<Solution>& outSolutions,
                          std::string currentJobId,
                          uint32_t currentEpoch,
                          uint64_t& totalHashes,
                          float& totalTimeMs);

private:
    PipelineManager* pipeline_;
    // Track hashes submitted per stream so we only add to totals when that stream's
    // previous batch has actually completed.
    std::vector<uint64_t> pendingCounts_;
};

}} // namespace
