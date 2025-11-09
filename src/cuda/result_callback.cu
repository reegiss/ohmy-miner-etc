#include "result_callback.hpp"
#include "ohmy/stratum_client.hpp"
#include "ohmy/logger.hpp"

namespace ohmy::cuda {

// Thread-local error tracking
thread_local std::string CallbackErrorTracker::lastError;
thread_local bool CallbackErrorTracker::hasError = false;

void CallbackErrorTracker::recordError(const std::string& error) {
    lastError = error;
    hasError = true;
    LOG_ERROR("[Callback] " + error);
}

std::string CallbackErrorTracker::getLastError() {
    return lastError;
}

void CallbackErrorTracker::clearError() {
    lastError.clear();
    hasError = false;
}

/**
 * Phase 4.3: Async callback for pool submission
 * 
 * This callback is invoked asynchronously to submit already-processed solutions
 * to the mining pool. Solutions are already copied from device to host before
 * this callback is invoked.
 * 
 * All operations are non-blocking from GPU perspective.
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
        // Solutions are already in CPU memory (copied in search() before callback)
        uint32_t numSolutions = cbData->solutions.size();
        
        if (numSolutions > 0) {
            LOG_DEBUG("[Callback] Submitting " + std::to_string(numSolutions) + " solution(s) to pool");
            
            // Submit solutions to pool if StratumClient is available
            if (cbData->stratumClient) {
                network::StratumClient* client = 
                    static_cast<network::StratumClient*>(cbData->stratumClient);
                
                for (uint32_t i = 0; i < numSolutions; ++i) {
                    const Solution& sol = cbData->solutions[i];
                    
                    // Submit to pool
                    bool submitOk = client->submitSolution(sol);
                    if (submitOk) {
                        LOG_DEBUG("[Callback] Solution " + std::to_string(i+1) + "/" + 
                                 std::to_string(numSolutions) + " submitted successfully");
                    } else {
                        LOG_WARN("[Callback] Failed to submit solution " + std::to_string(i+1));
                    }
                }
            } else {
                LOG_WARN("[Callback] StratumClient not set, solutions not submitted");
            }
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
