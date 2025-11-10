#include "ohmy/cuda/pipeline/pipeline_manager.hpp"
#include "ohmy/cuda/utils/cuda_check.hpp"
#include "ohmy/logger.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <mutex>
#include <deque>

namespace ohmy { namespace cuda {
// DIP/SOLID async pipeline interface
void PipelineManager::queueSearch(const hash32_t& headerHash,
                                 const hash32_t& seedHash,
                                 const uint8_t targetBE[32],
                                 uint64_t startNonce,
                                 uint64_t count) {
    // Simple implementation: immediately "finish" by marking work as done
    // This simulates async behavior for the DIP interface
    std::lock_guard<std::mutex> lock(workMutex_);
    WorkItem item;
    item.headerHash = headerHash;
    item.seedHash = seedHash;
    std::copy(targetBE, targetBE + 32, item.targetBE.begin());
    item.startNonce = startNonce;
    item.count = count;
    pendingWork_.push_back(item);
    // Mark as immediately finished for synchronous behavior
    finishedWork_.push_back(pendingWork_.size() - 1);
}

int PipelineManager::getFinishedStream() const {
    std::lock_guard<std::mutex> lock(workMutex_);
    if (!finishedWork_.empty()) {
        return 0; // Return stream 0
    }
    return -1;
}

std::vector<Solution> PipelineManager::getResults(int streamIdx) {
    std::lock_guard<std::mutex> lock(workMutex_);
    if (!finishedWork_.empty()) {
        size_t workIdx = finishedWork_.front();
        finishedWork_.pop_front();
        // For now, return empty solutions (no actual mining)
        return {};
    }
    return {};
}

int PipelineManager::initialize(size_t freeMem, size_t totalMem) {
    if (initialized_) {
        LOG_WARN("PipelineManager already initialized");
        return numStreams_;
    }
    
    // Calculate optimal stream count based on available memory
    // Each stream needs ~640 bytes (header + seedHash + target + solutions + count)
    const size_t bufferPerStream = 640;
    
    // Reserve 10% of free memory for other operations
    size_t availableForStreams = (freeMem * 90) / 100;
    int optimalStreams = std::max(MIN_STREAMS, static_cast<int>(availableForStreams / bufferPerStream));
    
    // Cap at reasonable maximum
    optimalStreams = std::min(optimalStreams, MAX_STREAMS);
    
    // Allow override via environment variable
    const char* streamsEnv = std::getenv("OHMY_NUM_STREAMS");
    numStreams_ = streamsEnv ? std::atoi(streamsEnv) : optimalStreams;
    
    // Clamp to valid range
    numStreams_ = std::max(MIN_STREAMS, std::min(numStreams_, MAX_STREAMS));
    
    LOG_INFO("Pipeline optimal streams: " + std::to_string(optimalStreams) + 
             " (using " + std::to_string(numStreams_) + 
             ", override with OHMY_NUM_STREAMS env var)");
    
    const uint32_t maxSolutions = 16;
    
    // Allocate host pinned buffers
    for (int i = 0; i < numStreams_; i++) {
        uint8_t* h_header = nullptr;
        uint8_t* h_seedHash = nullptr;
        uint8_t* h_target = nullptr;
        uint32_t* h_solutionCount = nullptr;
        DeviceSolution* h_solution = nullptr;
        
        CUDA_CHECK(cudaHostAlloc(&h_header, 32, cudaHostAllocDefault));
        CUDA_CHECK(cudaHostAlloc(&h_seedHash, 32, cudaHostAllocDefault));
        CUDA_CHECK(cudaHostAlloc(&h_target, 32, cudaHostAllocDefault));
        CUDA_CHECK(cudaHostAlloc(&h_solutionCount, sizeof(uint32_t), cudaHostAllocDefault));
        CUDA_CHECK(cudaHostAlloc(&h_solution, maxSolutions * sizeof(DeviceSolution), cudaHostAllocDefault));
        
        h_headers_.push_back(h_header);
        h_seedHashes_.push_back(h_seedHash);
        h_targets_.push_back(h_target);
        h_solutionCounts_.push_back(h_solutionCount);
        h_solutions_.push_back(h_solution);
    }
    
    // Allocate device buffers
    for (int i = 0; i < numStreams_; i++) {
        uint8_t* d_header = nullptr;
        uint8_t* d_seedHash = nullptr;
        uint8_t* d_target = nullptr;
        uint32_t* d_solutionCount = nullptr;
        DeviceSolution* d_solution = nullptr;
        
        CUDA_CHECK(cudaMalloc(&d_header, 32));
        CUDA_CHECK(cudaMalloc(&d_seedHash, 32));
        CUDA_CHECK(cudaMalloc(&d_target, 32));
        CUDA_CHECK(cudaMalloc(&d_solutionCount, sizeof(uint32_t)));
        CUDA_CHECK(cudaMalloc(&d_solution, maxSolutions * sizeof(DeviceSolution)));
        
        d_headers_.push_back(d_header);
        d_seedHashes_.push_back(d_seedHash);
        d_targets_.push_back(d_target);
        d_solutionCounts_.push_back(d_solutionCount);
        d_solutions_.push_back(d_solution);
    }
    
    // Create CUDA streams and events
    for (int i = 0; i < numStreams_; i++) {
        cudaStream_t stream = nullptr;
        cudaEvent_t event = nullptr;
        cudaEvent_t startEvent = nullptr;
        cudaEvent_t stopEvent = nullptr;
        
        CUDA_CHECK(cudaStreamCreate(&stream));
        CUDA_CHECK(cudaEventCreate(&event));
        CUDA_CHECK(cudaEventCreate(&startEvent));
        CUDA_CHECK(cudaEventCreate(&stopEvent));
        
        streams_.push_back(stream);
        events_.push_back(event);
        streamStartEvents_.push_back(startEvent);
        streamStopEvents_.push_back(stopEvent);
    }
    
    initialized_ = true;
    streamIdx_ = 0;
    
    LOG_INFO("PipelineManager initialized with " + std::to_string(numStreams_) + 
             " streams, " + std::to_string(numStreams_) + " pinned host buffers, and " + 
             std::to_string(numStreams_) + " device buffer sets");
    
    return numStreams_;
}

void PipelineManager::cleanup() {
    if (!initialized_) {
        return;
    }
    
    // Free host pinned buffers
    for (size_t i = 0; i < h_headers_.size(); i++) {
        if (h_headers_[i]) cudaFreeHost(h_headers_[i]);
        if (h_seedHashes_[i]) cudaFreeHost(h_seedHashes_[i]);
        if (h_targets_[i]) cudaFreeHost(h_targets_[i]);
        if (h_solutionCounts_[i]) cudaFreeHost(h_solutionCounts_[i]);
        if (h_solutions_[i]) cudaFreeHost(h_solutions_[i]);
    }
    h_headers_.clear();
    h_seedHashes_.clear();
    h_targets_.clear();
    h_solutionCounts_.clear();
    h_solutions_.clear();
    
    // Free device buffers
    for (size_t i = 0; i < d_headers_.size(); i++) {
        if (d_headers_[i]) cudaFree(d_headers_[i]);
        if (d_seedHashes_[i]) cudaFree(d_seedHashes_[i]);
        if (d_targets_[i]) cudaFree(d_targets_[i]);
        if (d_solutionCounts_[i]) cudaFree(d_solutionCounts_[i]);
        if (d_solutions_[i]) cudaFree(d_solutions_[i]);
    }
    d_headers_.clear();
    d_seedHashes_.clear();
    d_targets_.clear();
    d_solutionCounts_.clear();
    d_solutions_.clear();
    
    // Destroy streams and events
    for (size_t i = 0; i < streams_.size(); i++) {
        if (streams_[i]) cudaStreamDestroy(streams_[i]);
        if (events_[i]) cudaEventDestroy(events_[i]);
        if (i < streamStartEvents_.size() && streamStartEvents_[i]) 
            cudaEventDestroy(streamStartEvents_[i]);
        if (i < streamStopEvents_.size() && streamStopEvents_[i]) 
            cudaEventDestroy(streamStopEvents_[i]);
    }
    streams_.clear();
    events_.clear();
    streamStartEvents_.clear();
    streamStopEvents_.clear();
    
    initialized_ = false;
    numStreams_ = 0;
    streamIdx_ = 0;
    
    LOG_INFO("PipelineManager cleaned up");
}

void PipelineManager::stageWork(int idx, const uint8_t* headerHash, 
                                const uint8_t* seedHash, const uint8_t* targetBE, 
                                cudaStream_t stream) {
    // Copy to host pinned buffers
    std::memcpy(h_headers_[idx], headerHash, 32);
    std::memcpy(h_seedHashes_[idx], seedHash, 32);
    std::memcpy(h_targets_[idx], targetBE, 32);
    *h_solutionCounts_[idx] = 0;

    // Async transfer to device
    CUDA_CHECK(cudaMemcpyAsync(d_headers_[idx], h_headers_[idx], 32, 
                               cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemcpyAsync(d_seedHashes_[idx], h_seedHashes_[idx], 32, 
                               cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemcpyAsync(d_targets_[idx], h_targets_[idx], 32, 
                               cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemsetAsync(d_solutionCounts_[idx], 0, sizeof(uint32_t), stream));
}

uint32_t PipelineManager::collectResults(int idx, uint32_t maxSolutions, cudaStream_t stream) {
    // Async copy results from device to host
    CUDA_CHECK(cudaMemcpyAsync(h_solutionCounts_[idx], d_solutionCounts_[idx], 
                               sizeof(uint32_t), cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(h_solutions_[idx], d_solutions_[idx], 
                               maxSolutions * sizeof(DeviceSolution), 
                               cudaMemcpyDeviceToHost, stream));
    
    // Synchronize stream to ensure data is ready in host memory
    CUDA_CHECK(cudaStreamSynchronize(stream));
    
    return *h_solutionCounts_[idx];
}

}} // namespace ohmy::cuda
