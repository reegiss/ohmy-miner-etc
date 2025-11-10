#include "ohmy/cuda/pipeline/search_executor.hpp"
#include <stdexcept>
#include <cstring>

namespace ohmy { namespace cuda {

uint32_t SearchExecutor::runSync(const SearchWork& work,
                                 const uint32_t maxSolutions,
                                 const uint32_t* d_header,
                                 const uint32_t* d_seedHash,
                                 const uint8_t* d_target,
                                 DeviceSolution* d_solutions,
                                 uint32_t* d_solutionCount,
                                 std::vector<Solution>& outSolutions,
                                 cudaStream_t streamCompute,
                                 cudaStream_t streamMemory,
                                 cudaStream_t streamIO,
                                 cudaEvent_t startEvent,
                                 cudaEvent_t stopEvent,
                                 cudaEvent_t memoryDone,
                                 cudaEvent_t kernelDone,
                                 cudaEvent_t resultsDone) {
    // Reset counter
    uint32_t zero = 0;
    CUDA_CHECK(cudaMemcpyAsync(d_solutionCount, &zero, sizeof(uint32_t), cudaMemcpyHostToDevice, streamMemory));

    // Copy inputs
    CUDA_CHECK(cudaMemcpyAsync(const_cast<uint32_t*>(d_header), work.headerHash->data(), 32, cudaMemcpyHostToDevice, streamMemory));
    CUDA_CHECK(cudaMemcpyAsync(const_cast<uint32_t*>(d_seedHash), work.seedHash->data(), 32, cudaMemcpyHostToDevice, streamMemory));
    CUDA_CHECK(cudaMemcpyAsync(const_cast<uint8_t*>(d_target), work.targetBE, 32, cudaMemcpyHostToDevice, streamMemory));

    CUDA_CHECK(cudaEventRecord(memoryDone, streamMemory));
    CUDA_CHECK(cudaStreamWaitEvent(streamCompute, memoryDone));
    CUDA_CHECK(cudaEventRecord(startEvent, streamCompute));

    KernelLaunchParams klp{};
    klp.d_dag = work.d_dag;
    klp.dagSize = work.dagSize;
    klp.d_header = d_header;
    klp.d_seedHash = d_seedHash;
    klp.d_targetBE = d_target;
    klp.startNonce = work.startNonce;
    klp.searchCount = work.count;
    klp.d_solutions = d_solutions;
    klp.d_solutionCount = d_solutionCount;
    klp.maxSolutions = maxSolutions;
    klp.stream = streamCompute;
    klp.variant = KernelVariant::Auto;
    launch_search_kernel(klp);

    CUDA_CHECK(cudaEventRecord(kernelDone, streamCompute));
    CUDA_CHECK(cudaStreamWaitEvent(streamIO, kernelDone));
    CUDA_CHECK(cudaEventRecord(stopEvent, streamCompute));

    uint32_t numSolutions = 0;
    CUDA_CHECK(cudaMemcpy(&numSolutions, d_solutionCount, sizeof(uint32_t), cudaMemcpyDeviceToHost));
    if (numSolutions > 0) {
        numSolutions = std::min(numSolutions, maxSolutions);
        std::vector<DeviceSolution> tmp(numSolutions);
        CUDA_CHECK(cudaMemcpy(tmp.data(), d_solutions, numSolutions * sizeof(DeviceSolution), cudaMemcpyDeviceToHost));
        outSolutions.clear();
        outSolutions.reserve(numSolutions);
        for (uint32_t i = 0; i < numSolutions; ++i) {
            Solution sol;
            sol.nonce = tmp[i].nonce;
            std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
            std::memcpy(sol.result.data(), tmp[i].result, 32);
            sol.jobId = work.jobId;
            outSolutions.push_back(std::move(sol));
        }
    }
    return numSolutions;
}

uint32_t SearchExecutor::runAsyncTick(const SearchWork& newWork,
                                      const uint32_t maxSolutions,
                                      std::vector<Solution>& outSolutions,
                                      std::string currentJobId,
                                      uint32_t currentEpoch,
                                      uint64_t& totalHashes,
                                      float& totalTimeMs) {
    if (!pipeline_ || !pipeline_->isInitialized()) {
        LOG_WARN("SearchExecutor: pipeline not initialized; cannot run async tick.");
        return 0;
    }
    // Ensure per-stream pendingCounts_ is sized
    int streamCount = pipeline_->getStreamCount();
    if ((int)pendingCounts_.size() != streamCount) {
        pendingCounts_.assign(streamCount, 0);
    }

    int i = pipeline_->getCurrentStreamIndex();
    cudaStream_t stream = pipeline_->getStream(i);
    cudaEvent_t event = pipeline_->getEvent(i);

    cudaError_t status = cudaEventQuery(event);
    if (status == cudaErrorNotReady) {
        pipeline_->advanceStream();
        return 0; // still running previous work
    } else if (status != cudaSuccess && status != cudaErrorNotReady) {
        CUDA_CHECK(status);
    }

    // At this point, the previous batch on stream i has completed.
    // Collect its results and update totals for that completed batch.
    uint32_t found = pipeline_->collectResults(i, maxSolutions, stream);
    
    outSolutions.clear();
    if (found > 0) {
        found = std::min(found, maxSolutions);
        DeviceSolution* h_solutions = pipeline_->getHostSolutions(i);
        outSolutions.reserve(found);
        for (uint32_t s = 0; s < found; ++s) {
            Solution sol;
            sol.nonce = h_solutions[s].nonce;
            std::memcpy(sol.mixHash.data(), h_solutions[s].mixHash, 32);
            std::memcpy(sol.result.data(), h_solutions[s].result, 32);
            sol.jobId = currentJobId;
            outSolutions.push_back(std::move(sol));
        }
    }

    // Account hashes and time for the batch that just finished on stream i
    if (pendingCounts_[i] > 0) {
        // Measure elapsed time using the stream's start/stop events from the PREVIOUS batch
        cudaEvent_t prevStartEvt = pipeline_->getStartEvent(i);
        cudaEvent_t prevStopEvt  = pipeline_->getStopEvent(i);
        float ms = 0.0f;
        CUDA_CHECK(cudaEventElapsedTime(&ms, prevStartEvt, prevStopEvt));
        totalTimeMs += ms;
        totalHashes += pendingCounts_[i];
        // Debug logging for totalTimeMs and totalHashes
        // LOG_DEBUG("Stream " + std::to_string(i) + ": Elapsed time (ms) = " + std::to_string(ms));
        // LOG_DEBUG("Stream " + std::to_string(i) + ": Hashes processed = " + std::to_string(pendingCounts_[i]));
        // LOG_DEBUG("Total time (ms) = " + std::to_string(totalTimeMs));
        // LOG_DEBUG("Total hashes = " + std::to_string(totalHashes));
        pendingCounts_[i] = 0;
    }

    // Stage and launch NEW work on this now-free stream
    pipeline_->stageWork(i,
                         reinterpret_cast<const uint8_t*>(newWork.headerHash->data()),
                         reinterpret_cast<const uint8_t*>(newWork.seedHash->data()),
                         newWork.targetBE, stream);

    cudaEvent_t startEvt = pipeline_->getStartEvent(i);
    cudaEvent_t stopEvt  = pipeline_->getStopEvent(i);
    CUDA_CHECK(cudaEventRecord(startEvt, stream));

    // Launch kernel for the new batch
    uint8_t* d_header = pipeline_->getDeviceHeader(i);
    uint8_t* d_seed = pipeline_->getDeviceSeedHash(i);
    uint8_t* d_target = pipeline_->getDeviceTarget(i);
    uint32_t* d_solCount = pipeline_->getDeviceSolutionCount(i);
    DeviceSolution* d_sol = pipeline_->getDeviceSolutions(i);

    KernelLaunchParams klp{};
    klp.d_dag = newWork.d_dag;
    klp.dagSize = newWork.dagSize;
    klp.d_header = reinterpret_cast<const uint32_t*>(d_header);
    klp.d_seedHash = reinterpret_cast<const uint32_t*>(d_seed);
    klp.d_targetBE = reinterpret_cast<const uint8_t*>(d_target);
    klp.startNonce = newWork.startNonce;
    klp.searchCount = newWork.count;
    klp.d_solutions = d_sol;
    klp.d_solutionCount = d_solCount;
    klp.maxSolutions = maxSolutions;
    klp.stream = stream;
    klp.variant = KernelVariant::Auto;
    launch_search_kernel(klp);

    CUDA_CHECK(cudaEventRecord(stopEvt, stream));
    CUDA_CHECK(cudaEventRecord(event, stream));

    // Record how many hashes are now in-flight for this stream
    pendingCounts_[i] = newWork.count;

    // Move to next stream for the next tick
    pipeline_->advanceStream();

    return found;
}

}} // namespace
