#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include "ohmy/logger.hpp"
#include "ohmy/cuda/core/device_state.hpp"
#include "ohmy/cuda/core/mining_job_context.hpp"
#include "mining_thread.hpp"

// CUDA and helper macros
#include <cuda_runtime.h>

namespace ohmy { namespace cuda {

// Use the same CUDA_CHECK macro behavior by re-declaring a local helper
static inline void cudaCheck(cudaError_t err, const char* msg) {
    if (err != cudaSuccess) {
        LOG_ERROR(std::string("CUDA error: ") + cudaGetErrorString(err) + ": " + msg);
        throw std::runtime_error("CUDA error");
    }
}

void miningThreadLoop(DeviceState* state,
                      std::shared_ptr<MiningJobContext> jobContext,
                      void* /*stratumClient*/,
                      bool* /*isMultiGpuMode*/,
                      std::string* /*currentJobId*/,
                      uint32_t* /*currentEpoch*/) {
    try {
        cudaCheck(cudaSetDevice(state->deviceId), "set device");
        LOG_INFO(std::string("[Mining Thread] Started on GPU #") + std::to_string(state->deviceId));
        state->threadRunning.store(true);

        while (!state->stopRequested.load()) {
            std::shared_ptr<MiningJobContext> currentCtx;
            {
                std::lock_guard<std::mutex> lock(jobContext->jobMutex);
                currentCtx = jobContext;
            }

            if (!currentCtx || !currentCtx->isValid.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // TODO: invoke kernels and handle solution retrieval here
            // For now, just yield briefly as placeholder
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch (const std::exception& ex) {
        LOG_ERROR(std::string("[Mining Thread] Exception on GPU #") + std::to_string(state->deviceId) + ": " + ex.what());
        state->threadErrors.fetch_add(1);
    }

    state->threadRunning.store(false);
    LOG_INFO(std::string("[Mining Thread] Exiting on GPU #") + std::to_string(state->deviceId));
}

}} // namespace ohmy::cuda
