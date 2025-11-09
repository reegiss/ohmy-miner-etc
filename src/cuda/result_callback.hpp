#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <iomanip>
#include <sstream>
#include "ohmy/types.hpp"

/**
 * Phase 4: Async Result Processing with Callbacks
 * 
 * Enables GPU-CPU coordination where result processing and pool submission
 * happen asynchronously in a callback, allowing the GPU to continue
 * kernel execution without stalling.
 */

namespace ohmy::cuda {

// Forward declarations
class DeviceManager;

// Note: Solution and DeviceSolution types will be defined in device_manager.hpp
// or core headers. For now, we use opaque pointers.

/**
 * Data structure passed to async callback
 * Contains all necessary information for result processing
 */
struct ResultCallbackData {
    // Host-side solution data (already copied from device!)
    std::vector<Solution> solutions;  // CPU-side solutions ready for submission
    
    // CPU-side handlers (StratumClient*)
    void* stratumClient;              // Pool client for submitting solutions
    
    // Metadata
    std::string jobId;                // Job identifier for result submission
    uint32_t epoch;                   // DAG epoch for context
    
    // Phase 5: Per-device tracking (multi-GPU support)
    int deviceId;                     // GPU device ID that generated this callback
    uint64_t deviceHashesThisRound;   // Hashes computed on this GPU for this round
    float deviceTimeMilliseconds;     // Time spent on this GPU for this round
    
    ResultCallbackData()
        : stratumClient(nullptr),
          jobId(""),
          epoch(0),
          deviceId(-1),
          deviceHashesThisRound(0),
          deviceTimeMilliseconds(0.0f)
    {}
};

/**
 * Async callback function for result processing
 * 
 * This function is called asynchronously by CUDA after the GPU stream completes.
 * It runs on a CUDA callback thread (not the main GPU thread).
 * 
 * Key points:
 * - Called when stream_io_ reaches the point where cudaLaunchHostFunc was invoked
 * - Runs asynchronously, does NOT block GPU pipeline
 * - Must handle all errors internally (void return type)
 * - Must clean up allocated memory (owns ResultCallbackData)
 * - Must be thread-safe (may run while other callbacks execute)
 * 
 * @param userData Pointer to ResultCallbackData (allocated with new)
 */
void processAndSubmitResultsCallback(void* userData);

/**
 * Error tracking for async callbacks
 * Since callbacks return void, errors must be captured
 */
struct CallbackErrorTracker {
    static thread_local std::string lastError;
    static thread_local bool hasError;
    
    static void recordError(const std::string& error);
    static std::string getLastError();
    static void clearError();
};

/**
 * Phase 5: Per-device statistics tracking for multi-GPU mining
 * 
 * Tracks performance metrics for each GPU independently:
 * - Total hashes computed
 * - Total time spent mining
 * - Solutions found
 * - Callback invocations
 * - Error count
 */
struct DeviceStats {
    int deviceId;                      // GPU device ID
    uint64_t totalHashes;              // Total hashes computed on this GPU
    uint64_t totalTime_ms;             // Total time spent (milliseconds)
    uint64_t solutionsFound;           // Number of valid solutions found
    uint64_t callbackInvocations;      // Number of times callback was called
    uint64_t errorCount;               // Number of errors on this GPU
    std::string lastErrorMessage;      // Last error message
    std::atomic<bool> initialized;     // Whether stats are initialized
    mutable std::mutex statsMutex;     // Thread-safe access (mutable for const methods)
    
    DeviceStats(int id = -1)
        : deviceId(id),
          totalHashes(0),
          totalTime_ms(0),
          solutionsFound(0),
          callbackInvocations(0),
          errorCount(0),
          lastErrorMessage(""),
          initialized(false)
    {}
    
    /**
     * Update statistics with callback data
     */
    void updateFromCallback(const ResultCallbackData& cbData) {
        std::lock_guard<std::mutex> lock(statsMutex);
        totalHashes += cbData.deviceHashesThisRound;
        totalTime_ms += static_cast<uint64_t>(cbData.deviceTimeMilliseconds);
        solutionsFound += cbData.solutions.size();
        callbackInvocations++;
    }
    
    /**
     * Record an error for this device
     */
    void recordError(const std::string& errorMsg) {
        std::lock_guard<std::mutex> lock(statsMutex);
        errorCount++;
        lastErrorMessage = errorMsg;
    }
    
    /**
     * Get hashrate in MH/s
     */
    double getHashrate_MHs() const {
        std::lock_guard<std::mutex> lock(statsMutex);
        if (totalTime_ms == 0) return 0.0;
        // Convert: hashes / milliseconds * 1000 / 1,000,000 = MH/s
        return (double)totalHashes / (double)totalTime_ms / 1000.0;
    }
    
    /**
     * Get aggregated statistics as string
     */
    std::string toString() const {
        std::lock_guard<std::mutex> lock(statsMutex);
        std::ostringstream oss;
        oss << "GPU#" << deviceId << ": "
            << "Hashes=" << totalHashes
            << ", Time=" << totalTime_ms << "ms"
            << ", Solutions=" << solutionsFound
            << ", Callbacks=" << callbackInvocations
            << ", Errors=" << errorCount
            << ", Rate=" << std::fixed << std::setprecision(2)
            << getHashrate_MHs() << "MH/s";
        if (!lastErrorMessage.empty()) {
            oss << ", LastError=" << lastErrorMessage;
        }
        return oss.str();
    }
};

}  // namespace ohmy::cuda
