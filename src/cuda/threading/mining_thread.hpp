#ifndef OHMY_CUDA_MINING_THREAD_HPP
#define OHMY_CUDA_MINING_THREAD_HPP

#include <memory>
#include <string>
#include <cstdint>
#include <cuda_runtime.h>
#include "ohmy/cuda/core/device_state.hpp"
#include "ohmy/cuda/core/mining_job_context.hpp"

namespace ohmy { namespace cuda {

// Forward declaration to avoid requiring full Stratum type here
void miningThreadLoop(DeviceState* state,
                      std::shared_ptr<MiningJobContext> jobContext,
                      void* stratumClient,
                      bool* isMultiGpuMode,
                      std::string* currentJobId,
                      uint32_t* currentEpoch);

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_MINING_THREAD_HPP
