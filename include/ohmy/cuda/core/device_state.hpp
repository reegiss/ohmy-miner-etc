#ifndef OHMY_CUDA_DEVICE_STATE_HPP
#define OHMY_CUDA_DEVICE_STATE_HPP

#include <cuda_runtime.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <memory>
#include "ohmy/cuda/core/device_solution.hpp"

namespace ohmy {
namespace cuda {

/**
 * DeviceState
 * ----------------
 * Per-GPU runtime state container. Owns CUDA buffers, streams, events and
 * thread lifecycle flags for a single mining device. It does NOT allocate the
 * DAG itself (external component performs that) but keeps a pointer to it.
 *
 * Responsibilities:
 *  - Hold device-scoped pointers (header, seed hash, target, solutions)
 *  - Maintain mining thread control flags (threadRunning, stopRequested)
 *  - Track performance counters (totalHashes / totalTime)
 *  - Provide cleanup() to release CUDA resources deterministically
 *
 * Thread-safety:
 *  - Atomic flags protect concurrent shutdown / error increments
 *  - cleanup() must be called after thread termination; no internal locking.
 *
 * Lifetime:
 *  - Created during device enumeration
 *  - cleanup() invoked on miner shutdown or device failure recovery
 */
struct DeviceState {
    int deviceId;
    bool initialized{false};

    // CUDA memory pointers
    void* d_dag{nullptr};
    void* d_header{nullptr};
    void* d_seedHash{nullptr};
    void* d_target{nullptr};
    ohmy::cuda::DeviceSolution* d_solutions{nullptr};
    uint32_t* d_solutionCount{nullptr};
    size_t dagSize{0};

    // Nonce parameters
    uint32_t nonceOffset{0};
    uint32_t nonceRange{UINT32_MAX};

    // CUDA streams
    cudaStream_t stream_compute{nullptr};
    cudaStream_t stream_memory{nullptr};
    cudaStream_t stream_io{nullptr};

    // Events for synchronization
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    cudaEvent_t memoryDoneEvent{nullptr};
    cudaEvent_t kernelDoneEvent{nullptr};
    cudaEvent_t resultsDoneEvent{nullptr};

    // Statistics
    uint64_t totalHashes{0};
    float totalTime{0.0f};

    // Threading state
    std::unique_ptr<std::thread> miningThread;
    std::atomic<bool> threadRunning{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<uint32_t> threadErrors{0};

    // Release all owned CUDA resources. Safe to call multiple times (idempotent).
    void cleanup();
};

} // namespace cuda
} // namespace ohmy

#endif // DEVICE_STATE_H