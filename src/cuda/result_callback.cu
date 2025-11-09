#include "result_callback.hpp"
#include "ohmy/stratum_client.hpp"
#include "ohmy/logger.hpp"

#include <chrono>

namespace ohmy::cuda {

// Thread-local error tracking
thread_local std::string CallbackErrorTracker::lastError;
thread_local bool CallbackErrorTracker::hasError = false;

void CallbackErrorTracker::recordError(const std::string& error) {
    lastError = error;
    hasError = true;
    LOG_ERROR("[ResultCallback] " + error);
}

std::string CallbackErrorTracker::getLastError() {
    return lastError;
}

void CallbackErrorTracker::clearError() {
    lastError.clear();
    hasError = false;
}

/**
 * Phase 4.4: Async callback for pool submission with integrated logging
 * 
 * This callback is invoked asynchronously to submit already-processed solutions
 * to the mining pool. Solutions are already copied from device to host before
 * this callback is invoked.
 * 
 * Features:
 * - Comprehensive logging (job ID, epoch, submission count, latency)
 * - Callback latency tracking
 * - Per-solution submission logging
 * - Error tracking and reporting
 * 
 * All operations are non-blocking from GPU perspective.
 */
void processAndSubmitResultsCallback(void* userData) {
    // Record callback start time for latency tracking
    auto callbackStart = std::chrono::high_resolution_clock::now();
    
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
        // Step 1: Log callback invocation details
        uint32_t numSolutions = cbData->solutions.size();
        
        std::string jobIdShort = cbData->jobId.length() > 8 ? 
                                cbData->jobId.substr(0, 8) : cbData->jobId;
        
        LOG_DEBUG("[ResultCallback] Invoked: job=" + jobIdShort + "..., " +
                 "epoch=" + std::to_string(cbData->epoch) + ", " +
                 "solutions=" + std::to_string(numSolutions));
        
        if (numSolutions == 0) {
            LOG_DEBUG("[ResultCallback] No solutions to submit");
            return;
        }
        
        // Step 2: Validate pool client
        if (!cbData->stratumClient) {
            LOG_WARN("[ResultCallback] StratumClient not set - cannot submit solutions");
            CallbackErrorTracker::recordError("StratumClient pointer is null");
            return;
        }
        
        network::StratumClient* client = 
            static_cast<network::StratumClient*>(cbData->stratumClient);
        
        // Step 3: Submit each solution to pool
        uint32_t successCount = 0;
        uint32_t failureCount = 0;
        
        for (uint32_t i = 0; i < numSolutions; ++i) {
            const Solution& sol = cbData->solutions[i];
            
            // Log submission attempt
            LOG_DEBUG("[ResultCallback] Submitting solution " + std::to_string(i+1) + "/" + 
                     std::to_string(numSolutions) + " nonce=0x" +
                     std::to_string(sol.nonce));
            
            // Submit to pool
            bool submitOk = client->submitSolution(sol);
            
            if (submitOk) {
                successCount++;
                LOG_DEBUG("[ResultCallback] ✓ Solution " + std::to_string(i+1) + " accepted");
            } else {
                failureCount++;
                LOG_WARN("[ResultCallback] ✗ Solution " + std::to_string(i+1) + " rejected");
            }
        }
        
        // Step 4: Log completion and metrics
        auto callbackEnd = std::chrono::high_resolution_clock::now();
        auto callbackDuration = std::chrono::duration_cast<
            std::chrono::microseconds>(callbackEnd - callbackStart);
        
        LOG_DEBUG("[ResultCallback] Completion: success=" + std::to_string(successCount) + 
                 ", failed=" + std::to_string(failureCount) +
                 ", latency=" + std::to_string(callbackDuration.count()) + "µs");
        
        if (failureCount > 0) {
            CallbackErrorTracker::recordError(
                "Callback: " + std::to_string(failureCount) + "/" + 
                std::to_string(numSolutions) + " submissions failed"
            );
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
