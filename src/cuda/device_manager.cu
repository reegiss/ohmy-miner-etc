#include "ohmy/types.hpp"
#include <stdint.h>

#include "ohmy/types.hpp"
#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include "result_callback.hpp"
#include "modules/device_initializer.hpp"
#include "ohmy/cuda/core/device_state.hpp"
#include "ohmy/cuda/stats/device_stats.hpp"
#include "ohmy/cuda/threading/mining_thread.hpp"
#include "ohmy/cuda/utils/cuda_check.hpp"
#include "ohmy/cuda/pipeline/pipeline_manager.hpp"
#include "ohmy/cuda/core/device_solution.hpp"
#include "ohmy/cuda/core/mining_job_context.hpp"
#include "ohmy/cuda/kernels/kernel_launcher.hpp"
#include "ohmy/cuda/pipeline/search_executor.hpp"
#include "ohmy/cuda/utils/memory_profiler.hpp"
#include <cuda_runtime.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>

namespace ohmy {
namespace cuda {

// CUDA_CHECK now provided by shared header

// Struct matching the device-side solution layout (POD) included above

// Centralized kernel launcher abstraction included at top-level to avoid nested namespaces

// Phase 5.2: Shared mining job context for all GPU threads (already included at top-level)
// Phase 5: DeviceState included at top-level; avoid re-including inside namespace to prevent nested namespaces

class DeviceManager::Impl {
public:
    // PipelineManager pipelineManager_; // Already declared as public below
    Impl() {
        int deviceCount = 0;
        CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
        LOG_INFO("Found " + std::to_string(deviceCount) + " CUDA device(s)");
        
        // Initialize member variables (Phase 4: single-GPU fallback)
        d_dag_ = nullptr;
        dagSize_ = 0;
        stream_compute_ = nullptr;
        stream_memory_ = nullptr;
        stream_io_ = nullptr;
        startEvent_ = nullptr;
        stopEvent_ = nullptr;
        memoryDoneEvent_ = nullptr;
        kernelDoneEvent_ = nullptr;
        resultsDoneEvent_ = nullptr;
        totalHashes_ = 0;
        totalTime_ = 0.0f;
        pipelineStartTime_ = std::chrono::steady_clock::now();  // PHASE 6: Track when mining started
        useTexture_ = false;
        texDAG_ = 0;
        currentJobId_ = "";
        currentEpoch_ = 0;
        
        // Phase 5: Multi-GPU state
        totalDeviceCount_ = deviceCount;
        isMultiGpuMode_ = false;
        miningRunning_ = false;
    }

    ~Impl() {
        if (isMultiGpuMode_) {
            cleanupMultiGpu();
        } else {
            cleanup();
        }
    }

    std::vector<DeviceInfo> getDevices() const {
        std::vector<DeviceInfo> devices;
        int deviceCount = 0;
        
        CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
        
        for (int i = 0; i < deviceCount; ++i) {
            cudaDeviceProp prop;
            CUDA_CHECK(cudaGetDeviceProperties(&prop, i));
            
            size_t freeMem, totalMem;
            CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
            
            DeviceInfo info;
            info.deviceId = i;
            info.name = prop.name;
            info.totalMemory = totalMem;
            info.freeMemory = freeMem;
            info.computeCapability = prop.major * 10 + prop.minor;
            info.multiProcessorCount = prop.multiProcessorCount;
            info.maxThreadsPerBlock = prop.maxThreadsPerBlock;
            
            devices.push_back(info);
            
            LOG_INFO("Device " + std::to_string(i) + ": " + info.name);
            LOG_INFO("  Compute Capability: " + std::to_string(prop.major) + "." + std::to_string(prop.minor));
            LOG_INFO("  Memory: " + std::to_string(totalMem / (1024*1024)) + " MB");
        }
        
        return devices;
    }

    bool initDevice(int deviceId, const void* dag, size_t dagSize) {
        CUDA_CHECK(cudaSetDevice(deviceId));
        
        LOG_INFO("Initializing device " + std::to_string(deviceId));
        
        // MEMORY PROFILING: Snapshot before DAG allocation
        auto memBefore = MemoryProfiler::snapshot(deviceId);
        MemoryProfiler::log(memBefore);
        
        // Allocate GPU memory for DAG
        dagSize_ = dagSize;
        CUDA_CHECK(cudaMalloc(&d_dag_, dagSize));
        
        // Copy DAG to GPU
        LOG_INFO("Copying DAG to GPU (" + std::to_string(dagSize / (1024*1024)) + " MB)");
        CUDA_CHECK(cudaMemcpy(d_dag_, dag, dagSize, cudaMemcpyHostToDevice));
        
        // MEMORY PROFILING: Snapshot after DAG allocation
        auto memAfterDAG = MemoryProfiler::snapshot(deviceId);
        MemoryProfiler::logDiff(memBefore, memAfterDAG);
        
        // PHASE 6.1: Initialize async pipeline manager
        size_t freeMem, totalMem;
        CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
        pipelineManager_.initialize(freeMem, totalMem);
        
        // MEMORY PROFILING: Snapshot after pipeline initialization
        auto memAfterPipeline = MemoryProfiler::snapshot(deviceId);
        MemoryProfiler::logDiff(memAfterDAG, memAfterPipeline);
        MemoryProfiler::log(memAfterPipeline);
        
        {
            const uint64_t* dagHost = static_cast<const uint64_t*>(dag);
            std::stringstream ss;
            ss << "DAG item 0: " << std::hex;
            for (int i = 0; i < 4; i++) {
                ss << " 0x" << std::setw(16) << std::setfill('0') << dagHost[i];
            }
            LOG_DEBUG(ss.str());
            
            // Also log DAG item 1 (starts at offset 8 uint64s)
            ss.str("");
            ss << "DAG item 1: " << std::hex;
            for (int i = 0; i < 4; i++) {
                ss << " 0x" << std::setw(16) << std::setfill('0') << dagHost[8 + i];
            }
            LOG_DEBUG(ss.str());
        }
        
        // Texture memory support disabled due to CUDA linear texture size limitations
        // (cannot bind 4GB+ buffers needed for production Ethash DAGs)
        // See: docs/TEXTURE_MEMORY_RESULTS.md for detailed analysis
        useTexture_ = false;
        texDAG_ = 0;
        
        // Create THREE CUDA streams for true 3-stream pipeline (Phase 3)
        // stream_compute_: Kernel execution (compute-bound work)
        // stream_memory_: Memory transfers (DAG updates between jobs)
        // stream_io_: Network I/O and non-blocking operations (Phase 3 NEW)
        CUDA_CHECK(cudaStreamCreate(&stream_compute_));
        CUDA_CHECK(cudaStreamCreate(&stream_memory_));
        CUDA_CHECK(cudaStreamCreate(&stream_io_));  // Phase 3: Added for 3-stream pipeline
        
        // Create events for timing
        CUDA_CHECK(cudaEventCreate(&startEvent_));
        CUDA_CHECK(cudaEventCreate(&stopEvent_));
        
        // Phase 3: Create stream synchronization events for 3-stream pipeline
        CUDA_CHECK(cudaEventCreate(&memoryDoneEvent_));
        CUDA_CHECK(cudaEventCreate(&kernelDoneEvent_));
        CUDA_CHECK(cudaEventCreate(&resultsDoneEvent_));
        
        LOG_INFO("Device " + std::to_string(deviceId) + " initialized successfully with N-stream async pipeline (Phase 6)");
        pipelineInitialized_ = true;
        return true;
    }

    // GPU-FIRST: Zero-copy initialization - DAG already in VRAM
    bool initDeviceZeroCopy(int deviceId, void* d_dag, size_t dagSize) {
        CUDA_CHECK(cudaSetDevice(deviceId));
        
        LOG_INFO("Initializing device " + std::to_string(deviceId) + " (zero-copy, DAG already in VRAM)");
        
        // MEMORY PROFILING: Snapshot current state
        auto memBefore = MemoryProfiler::snapshot(deviceId);
        MemoryProfiler::log(memBefore);
        
        // Use existing GPU DAG pointer (no allocation, no copy)
        dagSize_ = dagSize;
        d_dag_ = d_dag;
        LOG_INFO("Using GPU-resident DAG (" + std::to_string(dagSize / (1024*1024)) + " MB) - zero copy");
        
        // PHASE 6.1: Initialize async pipeline manager
        size_t freeMem, totalMem;
        CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
        pipelineManager_.initialize(freeMem, totalMem);
        
        // MEMORY PROFILING: Snapshot after pipeline initialization
        auto memAfterPipeline = MemoryProfiler::snapshot(deviceId);
        MemoryProfiler::logDiff(memBefore, memAfterPipeline);
        MemoryProfiler::log(memAfterPipeline);
        
        // Texture memory disabled (same reason as initDevice)
        useTexture_ = false;
        texDAG_ = 0;
        
        // Create streams and events (same as initDevice)
        CUDA_CHECK(cudaStreamCreate(&stream_compute_));
        CUDA_CHECK(cudaStreamCreate(&stream_memory_));
        CUDA_CHECK(cudaStreamCreate(&stream_io_));
        CUDA_CHECK(cudaEventCreate(&startEvent_));
        CUDA_CHECK(cudaEventCreate(&stopEvent_));
        CUDA_CHECK(cudaEventCreate(&memoryDoneEvent_));
        CUDA_CHECK(cudaEventCreate(&kernelDoneEvent_));
        CUDA_CHECK(cudaEventCreate(&resultsDoneEvent_));
        
        LOG_INFO("Device " + std::to_string(deviceId) + " initialized (GPU-first, zero-copy, minimal RAM)");
        pipelineInitialized_ = true;
        return true;
    }

    uint32_t search(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t startNonce,
        uint64_t count,
        std::vector<Solution>& solutions
    ) {
        // Use synchronous search for accurate hashrate measurement
        return searchSync(headerHash, seedHash, targetBE, startNonce, count, solutions);
    }

    /**
     * @brief Synchronous search implementation for accurate hashrate measurement
     * 
     * This function performs a blocking search operation, waiting for kernel completion
     * before returning. This ensures accurate timing and hashrate calculations.
     */
    uint32_t searchSync(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t startNonce,
        uint64_t count,
        std::vector<Solution>& solutions
    ) {
        if (!pipelineInitialized_) {
            LOG_ERROR("Pipeline not initialized - cannot perform sync search");
            return 0;
        }

        // Use the default compute stream for synchronous operation
        cudaStream_t stream = stream_compute_;
        if (!stream) {
            LOG_ERROR("Compute stream not available for sync search");
            return 0;
        }

        // Allocate device memory for this search
        uint32_t* d_header = nullptr;
        uint32_t* d_seedHash = nullptr;
        uint8_t* d_targetBE = nullptr;
        DeviceSolution* d_solutions = nullptr;
        uint32_t* d_solutionCount = nullptr;

        const uint32_t maxSolutions = 16;
        size_t headerSize = 8 * sizeof(uint32_t);  // 32 bytes
        size_t seedHashSize = 8 * sizeof(uint32_t); // 32 bytes
        size_t targetSize = 32 * sizeof(uint8_t);   // 32 bytes
        size_t solutionsSize = maxSolutions * sizeof(DeviceSolution);
        size_t countSize = sizeof(uint32_t);

        CUDA_CHECK(cudaMalloc(&d_header, headerSize));
        CUDA_CHECK(cudaMalloc(&d_seedHash, seedHashSize));
        CUDA_CHECK(cudaMalloc(&d_targetBE, targetSize));
        CUDA_CHECK(cudaMalloc(&d_solutions, solutionsSize));
        CUDA_CHECK(cudaMalloc(&d_solutionCount, countSize));

        // Copy input data to device
        CUDA_CHECK(cudaMemcpyAsync(d_header, headerHash.data(), headerSize, cudaMemcpyHostToDevice, stream));
        CUDA_CHECK(cudaMemcpyAsync(d_seedHash, seedHash.data(), seedHashSize, cudaMemcpyHostToDevice, stream));
        CUDA_CHECK(cudaMemcpyAsync(d_targetBE, targetBE, targetSize, cudaMemcpyHostToDevice, stream));

        // Reset solution counter
        uint32_t zero = 0;
        CUDA_CHECK(cudaMemcpyAsync(d_solutionCount, &zero, countSize, cudaMemcpyHostToDevice, stream));

        // Launch kernel
        KernelLaunchParams klp{};
        klp.d_dag = reinterpret_cast<const uint64_t*>(d_dag_);
        klp.dagSize = dagSize_;
        klp.d_header = d_header;
        klp.d_seedHash = d_seedHash;
        klp.d_targetBE = d_targetBE;
        klp.startNonce = startNonce;
        klp.searchCount = count;
        klp.d_solutions = d_solutions;
        klp.d_solutionCount = d_solutionCount;
        klp.maxSolutions = maxSolutions;
        klp.stream = stream;
        klp.variant = KernelVariant::Auto;

        // Start timing
        cudaEvent_t startEvent, stopEvent;
        CUDA_CHECK(cudaEventCreate(&startEvent));
        CUDA_CHECK(cudaEventCreate(&stopEvent));
        CUDA_CHECK(cudaEventRecord(startEvent, stream));

        launch_search_kernel(klp);

        // Record stop time
        CUDA_CHECK(cudaEventRecord(stopEvent, stream));

        // Wait for completion
        CUDA_CHECK(cudaEventSynchronize(stopEvent));

        // Calculate elapsed time
        float milliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&milliseconds, startEvent, stopEvent));

        // Update timing statistics
        totalTime_ += milliseconds;
        totalHashes_ += count;

        // Read results
        uint32_t numSolutions = 0;
        CUDA_CHECK(cudaMemcpy(&numSolutions, d_solutionCount, countSize, cudaMemcpyDeviceToHost));

        if (numSolutions > 0) {
            numSolutions = std::min(numSolutions, maxSolutions);
            
            std::vector<DeviceSolution> tmp(numSolutions);
            CUDA_CHECK(cudaMemcpy(tmp.data(), d_solutions, numSolutions * sizeof(DeviceSolution), cudaMemcpyDeviceToHost));
            
            solutions.clear();
            solutions.reserve(numSolutions);
            for (uint32_t i = 0; i < numSolutions; ++i) {
                Solution sol;
                sol.nonce = tmp[i].nonce;
                sol.jobId = currentJobId_;
                std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
                std::memcpy(sol.result.data(), tmp[i].result, 32);
                solutions.push_back(std::move(sol));
            }

            LOG_INFO("Found " + std::to_string(numSolutions) + " solution(s) in sync search!");
        }

        // Cleanup
        CUDA_CHECK(cudaFree(d_header));
        CUDA_CHECK(cudaFree(d_seedHash));
        CUDA_CHECK(cudaFree(d_targetBE));
        CUDA_CHECK(cudaFree(d_solutions));
        CUDA_CHECK(cudaFree(d_solutionCount));
        CUDA_CHECK(cudaEventDestroy(startEvent));
        CUDA_CHECK(cudaEventDestroy(stopEvent));

        return numSolutions;
    }

    /**
     * @brief PHASE 6: Async tick-based pipeline search using N-buffering
     * 
     * This is the core of the multi-stream async pipeline. It's designed to be called
     * repeatedly from the main loop without blocking. The function manages 3 streams
     * (or NUM_STREAMS) in a round-robin fashion, allowing overlapping of:
     * - Work N-1: Host->Device memory transfer (HtoD on stream i-1)
     * - Work N: Kernel execution (on stream i)
     * - Work N+1: Device->Host result readback (DtoH on stream i+1)
     * 
     * Returns: Number of solutions found and processed from the COMPLETED stream,
     *          or 0 if the current stream is still running (non-blocking behavior)
     */
    uint32_t searchAsync(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t startNonce,
        uint64_t count,
        std::vector<Solution>& solutions
    ) {
        if (!pipelineInitialized_) {
            LOG_WARN("Pipeline not initialized, falling back to sync search()");
            return search(headerHash, seedHash, targetBE, startNonce, count, solutions);
        }

        SearchWork w{};
        w.d_dag = reinterpret_cast<const uint64_t*>(d_dag_);
        w.dagSize = dagSize_;
        w.headerHash = &headerHash;
        w.seedHash = &seedHash;
        w.targetBE = targetBE;
        w.startNonce = startNonce;
        w.count = count;

        const uint32_t maxSolutions = 16;
        uint32_t found = searchExecutor_.runAsyncTick(w, maxSolutions, solutions, currentJobId_, currentEpoch_, totalHashes_, totalTime_);
        // Always increment totalHashes_ by w.count for each batch, regardless of solutions found
        totalHashes_ += w.count;
        if (found > 0) {
            LOG_INFO("Found " + std::to_string(found) + " solution(s)!");
        }
        return found;
    }

    uint64_t getHashRate(int deviceId) const {
        // Calculate average hashrate in MH/s (megahashes per second)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pipelineStartTime_).count();
        if (elapsed <= 1) {
            LOG_WARN("Elapsed time too small, defaulting to 1 ms to prevent division errors.");
            elapsed = 1;
        }
        double seconds = elapsed / 1000.0;
        double hashrate_mhs = (double)totalHashes_ / seconds / 1e6;
        LOG_INFO("GPU #" + std::to_string(deviceId) + " hashrate: " + std::to_string(hashrate_mhs) + " MH/s");
        return hashrate_mhs;
    }

    /**
     * @brief Phase 5: Set mining job context (jobId and epoch)
     */
    void setMiningJobContext(const std::string& jobId, uint32_t epoch) {
        currentJobId_ = jobId;
        currentEpoch_ = epoch;
        LOG_INFO("Setting mining job context: jobId=" + jobId + ", epoch=" + std::to_string(epoch));
    }

    /**
     * @brief Phase 5: Initialize all available CUDA devices
     */
    int initializeAllDevices(const void* dag, size_t dagSize) {
        return DeviceInitializer::initializeAllDevices(dag, dagSize, getDeviceStates());
    }

    /**
     * @brief Phase 5: Initialize specific device with multi-GPU nonce distribution
     */
    bool initDeviceWithMultiGpu(int deviceId, int devicesTotal, 
                                const void* dag, size_t dagSize) {
        auto state = std::make_unique<DeviceState>();
        state->deviceId = deviceId;
        state->dagSize = dagSize;

        if (DeviceInitializer::initDevice(deviceId, dag, dagSize, *state)) {
            getDeviceStates()[deviceId] = std::move(state);
            return true;
        }
        return false;
    }

    /**
     * @brief Phase 5: Get count of initialized devices
     */
    int getDeviceCount() const {
        if (isMultiGpuMode_) {
            int count = 0;
            for (const auto& entry : deviceStates_) {
                if (entry.second && entry.second->initialized) count++;
            }
            return count;
        }
        return 0;  // Single-GPU mode or not initialized
    }

    /**
     * @brief Phase 5: Check if device is initialized
     */
    bool isDeviceInitialized(int deviceId) const {
        if (!isMultiGpuMode_) {
            return false;
        }
        auto it = deviceStates_.find(deviceId);
        return it != deviceStates_.end() && it->second && it->second->initialized;
    }

    /**
     * @brief Phase 5: Get device info from state
     */
    DeviceInfo getDeviceInfoFromState(int deviceId) const {
        DeviceInfo info;
        if (deviceId < 0 || deviceId >= static_cast<int>(deviceStates_.size())) {
            return info;
        }
        
        CUDA_CHECK(cudaSetDevice(deviceId));
        
        cudaDeviceProp prop;
        CUDA_CHECK(cudaGetDeviceProperties(&prop, deviceId));
        
        size_t freeMem, totalMem;
        CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
        
        info.deviceId = deviceId;
        info.name = prop.name;
        info.totalMemory = totalMem;
        info.freeMemory = freeMem;
        info.computeCapability = prop.major * 10 + prop.minor;
        info.multiProcessorCount = prop.multiProcessorCount;
        info.maxThreadsPerBlock = prop.maxThreadsPerBlock;
        
        return info;
    }

    // Statistics methods moved to device_stats module
    uint64_t getTotalHashRate() const { return DeviceStatsUtil::totalHashRate(deviceStates_); }
    std::vector<uint64_t> getAllHashRates() const { return DeviceStatsUtil::allHashRates(deviceStates_); }
    std::vector<std::string> getDeviceStatistics(int deviceId = -1) const { return DeviceStatsUtil::formatDeviceStatistics(deviceStates_, deviceId); }

    /**
     * @brief Phase 5: Get aggregate statistics across all devices
     */
    std::string getAggregateStatistics() const {
        uint64_t totalHashes = 0;
        float totalTime = 0.0f;
        
        for (const auto& entry : deviceStates_) {
            if (entry.second && entry.second->initialized) {
                totalHashes += entry.second->totalHashes;
                totalTime = std::max(totalTime, entry.second->totalTime);  // Take max time
            }
        }
        
        float seconds = totalTime > 0.0f ? totalTime / 1000.0f : 0.0f;
        double hashrate = seconds > 0.0 ? (double)totalHashes / seconds / 1e6 : 0.0;
        
        std::ostringstream oss;
        oss << "Aggregate: "
            << "TotalHashes=" << totalHashes << " "
            << "MaxTime=" << totalTime << "ms "
            << "Rate=" << std::fixed << std::setprecision(2) << hashrate << " MH/s";
        
        return oss.str();
    }

    /**
     * @brief Phase 5: Search on specific device with device-specific nonce range
     */
    uint32_t searchDevice(
        int deviceId,
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        std::vector<Solution>& solutions
    ) {
        if (!isMultiGpuMode_ || deviceId < 0 || deviceId >= static_cast<int>(deviceStates_.size())) {
            return 0;
        }
        
        auto it = deviceStates_.find(deviceId);
        if (it == deviceStates_.end() || !it->second) {
            return 0;
        }
        DeviceState* state = it->second.get();
        if (!state->initialized) {
            return 0;
        }
        
        CUDA_CHECK(cudaSetDevice(deviceId));
        
        const uint32_t maxSolutions = 16;
        
        // Reset solution counter
        uint32_t zero = 0;
        CUDA_CHECK(cudaMemcpyAsync(state->d_solutionCount, &zero, sizeof(uint32_t),
                                   cudaMemcpyHostToDevice, state->stream_memory));
        
        // Copy header, seedHash, and target to device
        CUDA_CHECK(cudaMemcpyAsync(state->d_header, headerHash.data(), 32,
                                   cudaMemcpyHostToDevice, state->stream_memory));
        CUDA_CHECK(cudaMemcpyAsync(state->d_seedHash, seedHash.data(), 32,
                                   cudaMemcpyHostToDevice, state->stream_memory));
        CUDA_CHECK(cudaMemcpyAsync(state->d_target, targetBE, 32,
                                   cudaMemcpyHostToDevice, state->stream_memory));
        
        // Record memory sync event
        CUDA_CHECK(cudaEventRecord(state->memoryDoneEvent, state->stream_memory));
        CUDA_CHECK(cudaStreamWaitEvent(state->stream_compute, state->memoryDoneEvent));
        
        // Start timing
        CUDA_CHECK(cudaEventRecord(state->startEvent, state->stream_compute));
        
        // Launch kernel with device-specific nonce range via centralized launcher
        {
            KernelLaunchParams klp{};
            klp.d_dag = reinterpret_cast<const uint64_t*>(state->d_dag);
            klp.dagSize = state->dagSize;
            klp.d_header = reinterpret_cast<const uint32_t*>(state->d_header);
            klp.d_seedHash = reinterpret_cast<const uint32_t*>(state->d_seedHash);
            klp.d_targetBE = reinterpret_cast<const uint8_t*>(state->d_target);
            klp.startNonce = state->nonceOffset;
            klp.searchCount = state->nonceRange;
            klp.d_solutions = state->d_solutions;
            klp.d_solutionCount = state->d_solutionCount;
            klp.maxSolutions = maxSolutions;
            klp.stream = state->stream_compute;
            klp.variant = KernelVariant::Auto;
            launch_search_kernel(klp);
        }
        
        // Record kernel completion
        CUDA_CHECK(cudaEventRecord(state->kernelDoneEvent, state->stream_compute));
        CUDA_CHECK(cudaStreamWaitEvent(state->stream_io, state->kernelDoneEvent));
        CUDA_CHECK(cudaEventRecord(state->stopEvent, state->stream_compute));
        
        // Read solutions from device
        uint32_t numSolutions = 0;
        CUDA_CHECK(cudaMemcpy(&numSolutions, state->d_solutionCount, sizeof(uint32_t),
                              cudaMemcpyDeviceToHost));
        
        if (numSolutions > 0) {
            numSolutions = std::min(numSolutions, maxSolutions);
            
            std::vector<DeviceSolution> tmp(numSolutions);
            CUDA_CHECK(cudaMemcpy(tmp.data(), state->d_solutions,
                                 numSolutions * sizeof(DeviceSolution), cudaMemcpyDeviceToHost));
            solutions.clear();
            solutions.reserve(numSolutions);
            for (uint32_t i = 0; i < numSolutions; ++i) {
                Solution sol;
                sol.nonce = tmp[i].nonce;
                sol.jobId = currentJobId_;
                std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
                std::memcpy(sol.result.data(), tmp[i].result, 32);
                solutions.push_back(std::move(sol));
            }
            
            LOG_INFO("GPU #" + std::to_string(deviceId) + " found " + 
                     std::to_string(numSolutions) + " solution(s)!");
        }
        
        // Removed callback and StratumClient submission for DIP/SOLID refactor
        
        // Update statistics
        float milliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&milliseconds, state->startEvent, state->stopEvent));
        state->totalHashes += state->nonceRange;
        state->totalTime += milliseconds;
        
        return numSolutions;
    }

    /**
     * @brief Phase 5.2: Start mining on all devices
     */
    void startMiningAllDevices(
        const hash32_t& headerHash,
        const hash32_t& seedHash,
        const uint8_t targetBE[32],
        uint64_t durationSeconds) {
        
        if (!isMultiGpuMode_) {
            LOG_WARN("startMiningAllDevices called but not in multi-GPU mode");
            return;
        }
        
        LOG_INFO("Starting mining on all devices for " + 
                (durationSeconds > 0 ? std::to_string(durationSeconds) + " seconds" : "indefinite duration"));
        
        // Create shared job context
        jobContext_ = std::make_shared<MiningJobContext>();
        jobContext_->jobId = std::string(headerHash.data(), headerHash.data() + 32);
        jobContext_->headerHash = headerHash;
        jobContext_->seedHash = seedHash;
        std::copy(targetBE, targetBE + 32, jobContext_->targetBE.begin());
        jobContext_->epoch = currentEpoch_;
        jobContext_->isValid.store(true);
        jobContext_->timestamp.store(
            std::chrono::system_clock::now().time_since_epoch().count());
        
        miningRunning_ = true;
        
        // Spawn mining thread for each device
        int threadCount = 0;
        for (auto& entry : deviceStates_) {
            auto& state = entry.second;
            if (!state || !state->initialized) continue;
            
            // Reset stop flag and clear thread if it exists
            state->stopRequested.store(false);
            state->threadErrors.store(0);
            if (state->miningThread && state->miningThread->joinable()) {
                state->miningThread->join();
            }
            
            // Spawn new mining thread for this device
            state->miningThread = std::make_unique<std::thread>(
                ohmy::cuda::miningThreadLoop,
                state.get(),
                jobContext_,
                nullptr,  // No StratumClient - solutions returned via queue
                &isMultiGpuMode_,
                &currentJobId_,
                &currentEpoch_
            );
            
            threadCount++;
            LOG_INFO("Spawned mining thread for GPU #" + std::to_string(state->deviceId));
        }
        
        LOG_INFO("Total mining threads spawned: " + std::to_string(threadCount));
    }

    /**
     * @brief Phase 5.2: Stop all mining threads
     */
    void stopAllMining() {
        LOG_INFO("Stopping all mining threads");
        
        if (!isMultiGpuMode_) {
            return;
        }
        
        // Signal all threads to stop
        for (auto& entry : deviceStates_) {
            auto& state = entry.second;
            if (!state || !state->initialized) continue;
            state->stopRequested.store(true);
        }
        
        // Wait for all threads to finish
        int stoppedCount = 0;
        for (auto& entry : deviceStates_) {
            auto& state = entry.second;
            if (!state || !state->initialized) continue;
            
            if (state->miningThread && state->miningThread->joinable()) {
                LOG_INFO("Waiting for mining thread on GPU #" + std::to_string(state->deviceId) + 
                         " to stop...");
                state->miningThread->join();
                stoppedCount++;
            }
        }
        
        LOG_INFO("All mining threads stopped (" + std::to_string(stoppedCount) + " threads)");
        miningRunning_ = false;
    }

    // Ensure deviceStates_ is accessible by making it public or providing a getter method
    public:
    std::map<int, std::unique_ptr<DeviceState>>& getDeviceStates() {
        return deviceStates_;
    }

private:
    // Cleanup all multi-GPU device states
    void cleanupMultiGpu() {
        // Stop all mining threads first
        stopAllMining();
        
        for (auto& entry : deviceStates_) {
            auto& state = entry.second;
            if (state && state->initialized) {
                CUDA_CHECK(cudaSetDevice(state->deviceId));
                state->cleanup();
            }
        }
        deviceStates_.clear();
        isMultiGpuMode_ = false;
        jobContext_.reset();
    }
    void cleanup() {
        // PHASE 6: Cleanup N-stream pipeline via manager
        pipelineManager_.cleanup();
        
        // Destroy texture object if it was created
        if (texDAG_ != 0) {
            cudaDestroyTextureObject(texDAG_);
            texDAG_ = 0;
        }
        
        if (d_dag_) {
            cudaFree(d_dag_);
            d_dag_ = nullptr;
        }
        
        if (stream_compute_) {
            cudaStreamDestroy(stream_compute_);
            stream_compute_ = nullptr;
        }
        if (stream_memory_) {
            cudaStreamDestroy(stream_memory_);
            stream_memory_ = nullptr;
        }
        if (stream_io_) {
            cudaStreamDestroy(stream_io_);
            stream_io_ = nullptr;
        }
        if (startEvent_) {
            cudaEventDestroy(startEvent_);
            startEvent_ = nullptr;
        }
        if (stopEvent_) {
            cudaEventDestroy(stopEvent_);
            stopEvent_ = nullptr;
        }
        if (memoryDoneEvent_) {
            cudaEventDestroy(memoryDoneEvent_);
            memoryDoneEvent_ = nullptr;
        }
        if (kernelDoneEvent_) {
            cudaEventDestroy(kernelDoneEvent_);
            kernelDoneEvent_ = nullptr;
        }
        if (resultsDoneEvent_) {
            cudaEventDestroy(resultsDoneEvent_);
            resultsDoneEvent_ = nullptr;
        }
    }
    
    // Device memory pointers
    void* d_dag_;
    size_t dagSize_;
    
public:
    // PHASE 6: N-Stream Pipeline Architecture via PipelineManager
    PipelineManager pipelineManager_;
    SearchExecutor searchExecutor_{&pipelineManager_};
    
    // Legacy 3-stream support (kept for backward compatibility with old event-based timing)
    cudaStream_t stream_compute_;
    cudaStream_t stream_memory_;
    cudaStream_t stream_io_;
    
    // Optimization flags and resources
    bool useTexture_;               // Whether texture memory is enabled
    cudaTextureObject_t texDAG_;    // Texture object for DAG access
    
    // Timing and statistics
    cudaEvent_t startEvent_;
    cudaEvent_t stopEvent_;
    
    // Phase 3: Stream synchronization events for 3-stream pipeline
    cudaEvent_t memoryDoneEvent_;
    cudaEvent_t kernelDoneEvent_;
    cudaEvent_t resultsDoneEvent_;
    
    // PHASE 6: Timing tracking for async pipeline
    std::chrono::steady_clock::time_point pipelineStartTime_;
    
    uint64_t totalHashes_;
    float totalTime_;
    
    // PHASE 6: Pipeline state for async tick-based search
    uint64_t lastStartNonce_ = 0;
    uint64_t lastSearchCount_ = 0;
    bool pipelineInitialized_ = false;
    
    // Phase 4: Callback context
    std::string currentJobId_;      // Current mining job ID
    uint32_t currentEpoch_;         // Current mining epoch
    
    // Phase 5: Multi-GPU state
    std::map<int, std::unique_ptr<DeviceState>> deviceStates_;  // Per-device state via map
    int totalDeviceCount_;                   // Total available GPUs in system
    bool isMultiGpuMode_;                    // Flag for multi-GPU vs single-GPU mode
    bool miningRunning_;                     // Flag for active mining threads
    std::shared_ptr<MiningJobContext> jobContext_;  // Phase 5.2: Shared job context for all threads
};

// DeviceManager implementation
DeviceManager::DeviceManager()
    : pImpl_(std::make_unique<Impl>())
{}

DeviceManager::~DeviceManager() = default;

std::vector<DeviceInfo> DeviceManager::getDevices() const {
    return pImpl_->getDevices();
}

bool DeviceManager::initDevice(int deviceId, const void* dag, size_t dagSize) {
    return pImpl_->initDevice(deviceId, dag, dagSize);
}

bool DeviceManager::initDeviceZeroCopy(int deviceId, void* d_dag, size_t dagSize) {
    return pImpl_->initDeviceZeroCopy(deviceId, d_dag, dagSize);
}

uint32_t DeviceManager::search(
    const hash32_t& headerHash,
    const hash32_t& seedHash,
    const uint8_t targetBE[32],
    uint64_t startNonce,
    uint64_t count,
    std::vector<Solution>& solutions
) {
    return pImpl_->search(headerHash, seedHash, targetBE, startNonce, count, solutions);
}

uint32_t DeviceManager::searchAsync(
    const hash32_t& headerHash,
    const hash32_t& seedHash,
    const uint8_t targetBE[32],
    uint64_t startNonce,
    uint64_t count,
    std::vector<Solution>& solutions
) {
    return pImpl_->searchAsync(headerHash, seedHash, targetBE, startNonce, count, solutions);
}

uint32_t DeviceManager::searchSync(
    const hash32_t& headerHash,
    const hash32_t& seedHash,
    const uint8_t targetBE[32],
    uint64_t startNonce,
    uint64_t count,
    std::vector<Solution>& solutions
) {
    return pImpl_->searchSync(headerHash, seedHash, targetBE, startNonce, count, solutions);
}

uint64_t DeviceManager::getHashRate(int deviceId) const {
    return pImpl_->getHashRate(deviceId);
}

void DeviceManager::setMiningJobContext(const std::string& jobId, uint32_t epoch) {
    pImpl_->setMiningJobContext(jobId, epoch);
}

// Phase 5: Multi-GPU wrapper methods
int DeviceManager::initializeAllDevices(const void* dag, size_t dagSize) {
    return DeviceInitializer::initializeAllDevices(dag, dagSize, pImpl_->getDeviceStates());
}

bool DeviceManager::initDeviceWithMultiGpu(int deviceId, int devicesTotal,
                                           const void* dag, size_t dagSize) {
    auto state = std::make_unique<DeviceState>();
    state->deviceId = deviceId;
    state->dagSize = dagSize;

    if (DeviceInitializer::initDevice(deviceId, dag, dagSize, *state)) {
        pImpl_->getDeviceStates()[deviceId] = std::move(state);
        return true;
    }
    return false;
}

int DeviceManager::getDeviceCount() const {
    return pImpl_->getDeviceCount();
}

bool DeviceManager::isDeviceInitialized(int deviceId) const {
    return pImpl_->isDeviceInitialized(deviceId);
}

DeviceInfo DeviceManager::getDeviceInfo(int deviceId) const {
    return pImpl_->getDeviceInfoFromState(deviceId);
}



uint32_t DeviceManager::searchDevice(
    int deviceId,
    const hash32_t& headerHash,
    const hash32_t& seedHash,
    const uint8_t targetBE[32],
    std::vector<Solution>& solutions
) {
    return pImpl_->searchDevice(deviceId, headerHash, seedHash, targetBE, solutions);
}

void DeviceManager::startMiningAllDevices(
    const hash32_t& headerHash,
    const hash32_t& seedHash,
    const uint8_t targetBE[32],
    uint64_t durationSeconds
) {
    pImpl_->startMiningAllDevices(headerHash, seedHash, targetBE, durationSeconds);
}

void DeviceManager::stopAllMining() {
    pImpl_->stopAllMining();
}

uint64_t DeviceManager::getTotalHashRate() const {
    return pImpl_->getTotalHashRate();
}

std::vector<uint64_t> DeviceManager::getAllHashRates() const {
    return pImpl_->getAllHashRates();
}

std::vector<std::string> DeviceManager::getDeviceStatistics(int deviceId) const {
    return pImpl_->getDeviceStatistics(deviceId);
}

std::string DeviceManager::getAggregateStatistics() const {
    return pImpl_->getAggregateStatistics();
}

// DIP/SOLID: Fine-grained async pipeline methods
void DeviceManager::queueSearch(const hash32_t& headerHash,
                                const hash32_t& seedHash,
                                const uint8_t targetBE[32],
                                uint64_t startNonce,
                                uint64_t count) {
    // Forward to pipeline manager (assume pImpl_->pipelineManager_ exists)
    pImpl_->pipelineManager_.queueSearch(headerHash, seedHash, targetBE, startNonce, count);
}

int DeviceManager::getFinishedStream() const {
    // Query pipeline manager for finished stream index
    return pImpl_->pipelineManager_.getFinishedStream();
}

std::vector<Solution> DeviceManager::getResults(int streamIdx) {
    // Retrieve results from pipeline manager
    return pImpl_->pipelineManager_.getResults(streamIdx);
}

} // namespace cuda
} // namespace ohmy
