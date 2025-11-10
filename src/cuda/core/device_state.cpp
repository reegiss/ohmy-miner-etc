#include "device_state.hpp"
#include <cuda_runtime.h>
#include <stdexcept>

namespace ohmy {
namespace cuda {

void DeviceState::cleanup() {
    if (miningThread) {
        stopRequested.store(true);
        if (miningThread->joinable()) {
            miningThread->join();
        }
        miningThread.reset();
    }

    if (d_dag) cudaFree(d_dag), d_dag = nullptr;
    if (d_header) cudaFree(d_header), d_header = nullptr;
    if (d_seedHash) cudaFree(d_seedHash), d_seedHash = nullptr;
    if (d_target) cudaFree(d_target), d_target = nullptr;
    if (stream_compute) cudaStreamDestroy(stream_compute), stream_compute = nullptr;
    if (stream_memory) cudaStreamDestroy(stream_memory), stream_memory = nullptr;
    if (stream_io) cudaStreamDestroy(stream_io), stream_io = nullptr;
    if (startEvent) cudaEventDestroy(startEvent), startEvent = nullptr;
    if (stopEvent) cudaEventDestroy(stopEvent), stopEvent = nullptr;
    if (memoryDoneEvent) cudaEventDestroy(memoryDoneEvent), memoryDoneEvent = nullptr;
    if (kernelDoneEvent) cudaEventDestroy(kernelDoneEvent), kernelDoneEvent = nullptr;
    if (resultsDoneEvent) cudaEventDestroy(resultsDoneEvent), resultsDoneEvent = nullptr;
    initialized = false;
}

} // namespace cuda
} // namespace ohmy