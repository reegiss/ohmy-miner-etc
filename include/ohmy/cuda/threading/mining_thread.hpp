#ifndef OHMY_CUDA_MINING_THREAD_HPP
#define OHMY_CUDA_MINING_THREAD_HPP

#include <memory>
#include <string>
#include <cstdint>
#include <cuda_runtime.h>
#include "ohmy/cuda/core/device_state.hpp"
#include "ohmy/cuda/core/mining_job_context.hpp"

namespace ohmy { namespace cuda {

// Forward declaration to avoid requiring full Stratum type here.
//
// miningThreadLoop
// ----------------
// Entry point for a per-GPU mining worker thread. It pulls job data from
// MiningJobContext, launches kernels (via higher-level abstractions) and
// submits found solutions through the opaque Stratum client pointer.
//
// Parameters:
//  - state: Mutable per-device state (buffers, counters, flags)
//  - jobContext: Shared job metadata (updated by network thread)
//  - stratumClient: Opaque handle (void*) to keep header free of protocol details
//  - isMultiGpuMode: Shared flag controlling multi-GPU coordination decisions
//  - currentJobId/currentEpoch: Shared tracking for stale job detection
//
// Thread-safety / Ownership:
//  - Function manages state->threadRunning and stopRequested semantics.
//  - Caller is responsible for lifecycle of jobContext & stratumClient.
//  - No internal locking beyond what DeviceState / MiningJobContext provide.
void miningThreadLoop(DeviceState* state,
                      std::shared_ptr<MiningJobContext> jobContext,
                      void* stratumClient,
                      bool* isMultiGpuMode,
                      std::string* currentJobId,
                      uint32_t* currentEpoch);

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_MINING_THREAD_HPP
