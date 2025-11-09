#include "result_callback.hpp"

#include <cuda_runtime.h>
#include <chrono>
#include <sstream>
#include <cstring>
#include <iostream>

namespace ohmy::cuda {

// Thread-local error tracking
thread_local std::string CallbackErrorTracker::lastError;
thread_local bool CallbackErrorTracker::hasError = false;

void CallbackErrorTracker::recordError(const std::string& error) {
    lastError = error;
    hasError = true;
    // LOG_ERROR("[Callback] " + error);  // TODO: conditional logging
}

std::string CallbackErrorTracker::getLastError() {
    return lastError;
}

void CallbackErrorTracker::clearError() {
    lastError.clear();
    hasError = false;
}

/**
 * Phase 4: Async callback for result processing
 * 
 * This callback is invoked asynchronously after the GPU kernel completes.
 * It runs on the CUDA callback thread and handles result processing.
 */
void processAndSubmitResultsCallback(void* userData) {
    // Safety check
    if (!userData) {
        CallbackErrorTracker::recordError("Callback received null userData");
        return;
    }
    
    // Unpack callback data
    std::unique_ptr<ResultCallbackData> cbData(
        static_cast<ResultCallbackData*>(userData)
    );
    
    try {
        // Step 1: Read solution count from device
        uint32_t numSolutions = 0;
        if (cbData->d_solutionCount) {
            cudaError_t err = cudaMemcpy(
                &numSolutions,
                cbData->d_solutionCount,
                sizeof(uint32_t),
                cudaMemcpyDeviceToHost
            );
            
            if (err != cudaSuccess) {
                std::string msg = "Failed to read solution count: ";
                msg += cudaGetErrorString(err);
                CallbackErrorTracker::recordError(msg);
                return;
            }
        }
        
        // Step 2: Log result
        if (numSolutions > 0) {
            numSolutions = std::min(numSolutions, cbData->maxSolutions);
            // LOG_INFO would be used here with proper logger integration
            std::cerr << "Callback: Found " << numSolutions << " solution(s)" << std::endl;
        }
        
    } catch (const std::exception& e) {
        CallbackErrorTracker::recordError(
            std::string("Exception in callback: ") + e.what()
        );
    } catch (...) {
        CallbackErrorTracker::recordError("Unknown exception in callback");
    }
    
    // cbData is automatically deleted via unique_ptr in dtor
}

}  // namespace ohmy::cuda
