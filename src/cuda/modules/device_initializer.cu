#include "device_initializer.hpp"
#include "ohmy/cuda/core/device_state.hpp"
#include "ohmy/cuda/utils/cuda_check.hpp"
#include <cuda_runtime.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace ohmy {
namespace cuda {

// Define CUDA_CHECK locally for this TU
bool DeviceInitializer::initDevice(int deviceId, const void* dag, size_t dagSize, DeviceState& state) {
    try {
        CUDA_CHECK(cudaSetDevice(deviceId));
        LOG_INFO("Initializing device " + std::to_string(deviceId));

        // Allocate GPU memory for DAG
        CUDA_CHECK(cudaMalloc(&state.d_dag, dagSize));
        CUDA_CHECK(cudaMemcpy(state.d_dag, dag, dagSize, cudaMemcpyHostToDevice));

        // Allocate other device memory
        CUDA_CHECK(cudaMalloc(&state.d_header, 32));
        CUDA_CHECK(cudaMalloc(&state.d_seedHash, 32));
        CUDA_CHECK(cudaMalloc(&state.d_target, 32));
    CUDA_CHECK(cudaMalloc(&state.d_solutions, 16 * sizeof(ohmy::cuda::DeviceSolution)));
        CUDA_CHECK(cudaMalloc(&state.d_solutionCount, sizeof(uint32_t)));

        // Create CUDA streams and events
        CUDA_CHECK(cudaStreamCreate(&state.stream_compute));
        CUDA_CHECK(cudaStreamCreate(&state.stream_memory));
        CUDA_CHECK(cudaStreamCreate(&state.stream_io));
        CUDA_CHECK(cudaEventCreate(&state.startEvent));
        CUDA_CHECK(cudaEventCreate(&state.stopEvent));
        CUDA_CHECK(cudaEventCreate(&state.memoryDoneEvent));
        CUDA_CHECK(cudaEventCreate(&state.kernelDoneEvent));
        CUDA_CHECK(cudaEventCreate(&state.resultsDoneEvent));

        state.initialized = true;
        LOG_INFO("Device " + std::to_string(deviceId) + " initialized successfully.");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize device " + std::to_string(deviceId) + ": " + e.what());
        state.cleanup();
        return false;
    }
}

int DeviceInitializer::initializeAllDevices(const void* dag, size_t dagSize, std::map<int, std::unique_ptr<DeviceState>>& deviceStates) {
    int deviceCount = 0;
    CUDA_CHECK(cudaGetDeviceCount(&deviceCount));

    if (deviceCount <= 0) {
        LOG_ERROR("No CUDA devices found!");
        return 0;
    }

    LOG_INFO("Initializing all " + std::to_string(deviceCount) + " CUDA devices.");
    int successCount = 0;

    for (int i = 0; i < deviceCount; ++i) {
        auto state = std::make_unique<DeviceState>();
        state->deviceId = i;
        state->dagSize = dagSize;

        if (initDevice(i, dag, dagSize, *state)) {
            deviceStates[i] = std::move(state);
            ++successCount;
        }
    }

    LOG_INFO("Successfully initialized " + std::to_string(successCount) + " device(s).");
    return successCount;
}

} // namespace cuda
} // namespace ohmy