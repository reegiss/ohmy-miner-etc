#ifndef OHMY_CUDA_UTILS_MEMORY_PROFILER_HPP
#define OHMY_CUDA_UTILS_MEMORY_PROFILER_HPP

#include <cstdint>
#include <string>
#include <chrono>

namespace ohmy { namespace cuda {

/**
 * @brief Snapshot of GPU memory state at a point in time
 */
struct MemorySnapshot {
    int deviceId = 0;
    size_t freeBytes = 0;
    size_t usedBytes = 0;
    size_t totalBytes = 0;
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * @brief Simple utility for profiling GPU memory usage
 */
class MemoryProfiler {
public:
    /**
     * @brief Capture current memory state for a device
     */
    static MemorySnapshot snapshot(int deviceId = 0);
    
    /**
     * @brief Format a memory snapshot as a human-readable string
     */
    static std::string format(const MemorySnapshot& snap);
    
    /**
     * @brief Compute and format the difference between two snapshots
     */
    static std::string diff(const MemorySnapshot& before, const MemorySnapshot& after);
    
    /**
     * @brief Log a snapshot to the logger
     */
    static void log(const MemorySnapshot& snap);
    
    /**
     * @brief Log the difference between two snapshots
     */
    static void logDiff(const MemorySnapshot& before, const MemorySnapshot& after);
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_UTILS_MEMORY_PROFILER_HPP
