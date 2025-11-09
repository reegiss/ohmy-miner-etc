#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>
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
    
    ResultCallbackData()
        : stratumClient(nullptr),
          jobId(""),
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
