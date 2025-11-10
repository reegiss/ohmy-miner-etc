#include "ohmy/cuda/utils/memory_profiler.hpp"
#include "ohmy/cuda/utils/cuda_check.hpp"
#include "ohmy/logger.hpp"
#include <sstream>
#include <iomanip>

namespace ohmy { namespace cuda {

MemorySnapshot MemoryProfiler::snapshot(int deviceId) {
    MemorySnapshot snap;
    snap.deviceId = deviceId;
    
    CUDA_CHECK(cudaSetDevice(deviceId));
    
    size_t free, total;
    CUDA_CHECK(cudaMemGetInfo(&free, &total));
    
    snap.freeBytes = free;
    snap.totalBytes = total;
    snap.usedBytes = total - free;
    snap.timestamp = std::chrono::steady_clock::now();
    
    return snap;
}

std::string MemoryProfiler::format(const MemorySnapshot& snap) {
    std::ostringstream oss;
    oss << "GPU[" << snap.deviceId << "] Memory: "
        << (snap.usedBytes / (1024.0 * 1024.0)) << " MB used, "
        << (snap.freeBytes / (1024.0 * 1024.0)) << " MB free, "
        << (snap.totalBytes / (1024.0 * 1024.0)) << " MB total ("
        << std::fixed << std::setprecision(1) 
        << (100.0 * snap.usedBytes / snap.totalBytes) << "% utilized)";
    return oss.str();
}

std::string MemoryProfiler::diff(const MemorySnapshot& before, const MemorySnapshot& after) {
    if (before.deviceId != after.deviceId) {
        return "Error: snapshots from different devices";
    }
    
    int64_t deltaUsed = static_cast<int64_t>(after.usedBytes) - static_cast<int64_t>(before.usedBytes);
    int64_t deltaFree = static_cast<int64_t>(after.freeBytes) - static_cast<int64_t>(before.freeBytes);
    
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        after.timestamp - before.timestamp).count();
    
    std::ostringstream oss;
    oss << "GPU[" << before.deviceId << "] Memory Delta: "
        << (deltaUsed > 0 ? "+" : "") << (deltaUsed / (1024.0 * 1024.0)) << " MB used, "
        << (deltaFree > 0 ? "+" : "") << (deltaFree / (1024.0 * 1024.0)) << " MB free "
        << "(elapsed: " << elapsedMs << " ms)";
    return oss.str();
}

void MemoryProfiler::log(const MemorySnapshot& snap) {
    LOG_INFO(format(snap));
}

void MemoryProfiler::logDiff(const MemorySnapshot& before, const MemorySnapshot& after) {
    LOG_INFO(diff(before, after));
}

}} // namespace ohmy::cuda
