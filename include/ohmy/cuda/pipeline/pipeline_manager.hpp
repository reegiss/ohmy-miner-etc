#include "ohmy/types.hpp"
#include <stdint.h>
#include <cstdint>
#ifndef OHMY_CUDA_PIPELINE_MANAGER_HPP
#define OHMY_CUDA_PIPELINE_MANAGER_HPP



#include <cuda_runtime.h>
#include <stdint.h>
#include <vector>
#include <mutex>
#include <deque>
#include <array>
#include "ohmy/types.hpp"
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
/**
 * PipelineManager
 * ----------------
 * Owns and orchestrates an N-stream asynchronous pipeline for mining jobs.
 * Each stream has its own host pinned buffers and matching device buffers to
 * overlap memcpy, kernel execution, and result collection.
 *
 * Stream Count Strategy:
 *  - Minimum 2 streams to allow overlap (memcpy + compute)
 *  - Maximum 8 to avoid excessive fragmentation / register pressure
 *  - DEFAULT_STREAMS used when heuristic cannot determine optimal count
 *  - initialize(freeMem, totalMem) may downscale based on available memory
 *
 * Usage Pattern:
 *  1. initialize() after DAG allocation (pass remaining free memory)
 *  2. For each job batch: use getCurrentStreamIndex(), perform transfers & launches
 *  3. advanceStream() to rotate streams (round-robin scheduling)
 *  4. cleanup() on shutdown
 *
 * Threading:
 *  - External synchronization required if multiple host threads access.
 *  - Typical usage confines mutations to a single manager thread.
 */
class PipelineManager {
public:
    // DIP/SOLID async pipeline interface
    void queueSearch(const hash32_t& headerHash,
                     const hash32_t& seedHash,
                     const uint8_t targetBE[32],
                     uint64_t startNonce,
                     uint64_t count);

    int getFinishedStream() const;

    std::vector<Solution> getResults(int streamIdx);
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

    /**
     * @brief Stage new work: copy header/seed/target from host to pinned buffers and then to device
     * @param idx Stream index
     * @param headerHash 32-byte header hash
     * @param seedHash 32-byte seed hash
     * @param targetBE 32-byte target (big-endian)
     * @param stream CUDA stream for async transfers
     */
    void stageWork(int idx, const uint8_t* headerHash, const uint8_t* seedHash, 
                   const uint8_t* targetBE, cudaStream_t stream);

    /**
     * @brief Collect results from device: copy solution count and solutions back to host
     * @param idx Stream index
     * @param maxSolutions Maximum number of solutions to copy
     * @param stream CUDA stream for async transfers
     * @return Number of solutions found (read from host buffer after sync)
     */
    uint32_t collectResults(int idx, uint32_t maxSolutions, cudaStream_t stream);

private:
    static constexpr int MIN_STREAMS = 2;
    static constexpr int MAX_STREAMS = 8;
    static constexpr int DEFAULT_STREAMS = 3;
    
    int numStreams_ = 0;
    int streamIdx_ = 0;
    bool initialized_ = false;
    
    // Work queue for DIP interface
    mutable std::mutex workMutex_;
    struct WorkItem {
        hash32_t headerHash;
        hash32_t seedHash;
        std::array<uint8_t, 32> targetBE;
        uint64_t startNonce;
        uint64_t count;
    };
    std::vector<WorkItem> pendingWork_;
    std::deque<size_t> finishedWork_;
    
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
