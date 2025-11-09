#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>

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
    // GPU-side pointers (device memory)
    uint32_t* d_solutionCount;       // Device pointer to solution count
    void* d_solutions;               // Device pointer to solutions array (opaque)
    
    // Parameters
    uint32_t maxSolutions;           // Maximum solutions to read
    
    // CPU-side handlers
    void* poolClient;                // Pool connection (opaque pointer)
    void* onResultsReady;            // Optional callback (opaque)
    
    // Metadata
    uint64_t jobId;                  // Job identifier for logging
    uint32_t epoch;                  // DAG epoch for context
    
    ResultCallbackData()
        : d_solutionCount(nullptr),
          d_solutions(nullptr),
          maxSolutions(16),
          poolClient(nullptr),
          onResultsReady(nullptr),
          jobId(0),
          epoch(0)
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

}  // namespace ohmy::cuda
