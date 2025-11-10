#ifndef OHMY_CUDA_PIPELINE_MANAGER_HPP
#define OHMY_CUDA_PIPELINE_MANAGER_HPP

#include <cuda_runtime.h>
#include <cstdint>
#include <vector>
#include "ohmy/cuda/core/device_solution.hpp"

namespace ohmy { namespace cuda {

/**
 * @brief Manages N-stream async pipeline buffers and orchestration
 * 
 * Encapsulates:
 * - Dynamic stream count based on available GPU memory
 * - Host pinned and device buffer allocation/management
 * - Round-robin stream indexing
 * - Stream/event creation and cleanup
 */
class PipelineManager {
public:
    /**
     * @brief Initialize pipeline with optimal stream count based on available memory
     * @param freeMem Free GPU memory in bytes (after DAG allocation)
     * @param totalMem Total GPU memory in bytes
     * @return Number of streams allocated
     */
    int initialize(size_t freeMem, size_t totalMem);
    
    /**
     * @brief Clean up all pipeline resources
     */
    void cleanup();
    
    /**
     * @brief Get current stream index (round-robin)
     */
    int getCurrentStreamIndex() const { return streamIdx_; }
    
    /**
     * @brief Advance to next stream (round-robin)
     */
    void advanceStream() { streamIdx_ = (streamIdx_ + 1) % numStreams_; }
    
    /**
     * @brief Get number of streams
     */
    int getStreamCount() const { return numStreams_; }
    
    /**
     * @brief Check if pipeline is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    // Buffer accessors
    cudaStream_t getStream(int idx) const { return streams_[idx]; }
    cudaEvent_t getEvent(int idx) const { return events_[idx]; }
    cudaEvent_t getStartEvent(int idx) const { return streamStartEvents_[idx]; }
    cudaEvent_t getStopEvent(int idx) const { return streamStopEvents_[idx]; }
    
    uint8_t* getHostHeader(int idx) const { return h_headers_[idx]; }
    uint8_t* getHostSeedHash(int idx) const { return h_seedHashes_[idx]; }
    uint8_t* getHostTarget(int idx) const { return h_targets_[idx]; }
    uint32_t* getHostSolutionCount(int idx) const { return h_solutionCounts_[idx]; }
    DeviceSolution* getHostSolutions(int idx) const { return h_solutions_[idx]; }
    
    uint8_t* getDeviceHeader(int idx) const { return d_headers_[idx]; }
    uint8_t* getDeviceSeedHash(int idx) const { return d_seedHashes_[idx]; }
    uint8_t* getDeviceTarget(int idx) const { return d_targets_[idx]; }
    uint32_t* getDeviceSolutionCount(int idx) const { return d_solutionCounts_[idx]; }
    DeviceSolution* getDeviceSolutions(int idx) const { return d_solutions_[idx]; }

private:
    static constexpr int MIN_STREAMS = 2;
    static constexpr int MAX_STREAMS = 8;
    static constexpr int DEFAULT_STREAMS = 3;
    
    int numStreams_ = 0;
    int streamIdx_ = 0;
    bool initialized_ = false;
    
    // Host pinned buffers (N copies)
    std::vector<uint8_t*> h_headers_;
    std::vector<uint8_t*> h_seedHashes_;
    std::vector<uint8_t*> h_targets_;
    std::vector<uint32_t*> h_solutionCounts_;
    std::vector<DeviceSolution*> h_solutions_;
    
    // Device buffers (N copies)
    std::vector<uint8_t*> d_headers_;
    std::vector<uint8_t*> d_seedHashes_;
    std::vector<uint8_t*> d_targets_;
    std::vector<uint32_t*> d_solutionCounts_;
    std::vector<DeviceSolution*> d_solutions_;
    
    // CUDA streams and events
    std::vector<cudaStream_t> streams_;
    std::vector<cudaEvent_t> events_;
    std::vector<cudaEvent_t> streamStartEvents_;
    std::vector<cudaEvent_t> streamStopEvents_;
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_PIPELINE_MANAGER_HPP
