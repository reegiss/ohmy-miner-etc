#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include "result_callback.hpp"
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

// CUDA error checking macro
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            LOG_ERROR("CUDA error: " + std::string(cudaGetErrorString(err))); \
            throw std::runtime_error("CUDA error"); \
        } \
    } while(0)

// Struct matching the device-side solution layout (POD)
struct DeviceSolution {
    uint64_t nonce;
    uint8_t mixHash[32];
    uint8_t result[32];
};

// Forward declaration of kernel launch functions
extern "C" void launch_ethash_search(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);

extern "C" void launch_ethash_search_optimized(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);

extern "C" void launch_ethash_search_texture(
    cudaTextureObject_t texDAG,
    uint64_t dagSize,
    const uint32_t* d_header,
    const uint32_t* d_seedHash,
    const uint8_t* d_targetBE,
    uint64_t startNonce,
    uint64_t searchCount,
    uint32_t noncesPerThread,
    DeviceSolution* d_solutions,
    uint32_t* d_solutionCount,
    uint32_t maxSolutions,
    cudaStream_t stream
);

// Phase 5.2: Shared mining job context for all GPU threads
struct MiningJobContext {
    std::string jobId;
    hash32_t headerHash;
    hash32_t seedHash;
    std::array<uint8_t, 32> targetBE;
    uint32_t epoch;
    std::atomic<bool> isValid{false};
    std::atomic<uint64_t> timestamp{0};
    std::mutex jobMutex;
};

// Phase 5: Per-device state structure for multi-GPU support
struct DeviceState {
    int deviceId;
    bool initialized{false};
    
    // CUDA memory pointers
    void* d_dag{nullptr};
    void* d_header{nullptr};
    void* d_seedHash{nullptr};
    DeviceSolution* d_solutions{nullptr};
    uint32_t* d_solutionCount{nullptr};
    void* d_target{nullptr};
    size_t dagSize{0};
    
    // CUDA streams (3-stream pipeline per device)
    cudaStream_t stream_compute{nullptr};
    cudaStream_t stream_memory{nullptr};
    cudaStream_t stream_io{nullptr};
    
    // Events for synchronization
    cudaEvent_t startEvent{nullptr};
    cudaEvent_t stopEvent{nullptr};
    cudaEvent_t memoryDoneEvent{nullptr};
    cudaEvent_t kernelDoneEvent{nullptr};
    cudaEvent_t resultsDoneEvent{nullptr};
    
    // Statistics
    uint64_t totalHashes{0};
    float totalTime{0.0f};
    
    // Nonce distribution (Phase 5)
    uint32_t nonceOffset{0};
    uint32_t nonceRange{UINT32_MAX};
    
    // Phase 5.2: Threading state
    std::unique_ptr<std::thread> miningThread;
    std::atomic<bool> threadRunning{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<uint32_t> threadErrors{0};
    
    // Cleanup method
    void cleanup() {
        // Stop and join thread if running
        if (miningThread) {
            stopRequested.store(true);
            if (miningThread->joinable()) {
                miningThread->join();
            }
            miningThread.reset();
        }
        
        if (d_dag) cudaFree(d_dag), d_dag = nullptr;
        if (d_header) cudaFree(d_header), d_header = nullptr;
        if (d_seedHash) cudaFree(d_seedHash), d_seedHash = nullptr;
        if (d_solutions) cudaFree(d_solutions), d_solutions = nullptr;
        if (d_solutionCount) cudaFree(d_solutionCount), d_solutionCount = nullptr;
        if (d_target) cudaFree(d_target), d_target = nullptr;
        if (stream_compute) cudaStreamDestroy(stream_compute), stream_compute = nullptr;
        if (stream_memory) cudaStreamDestroy(stream_memory), stream_memory = nullptr;
        if (stream_io) cudaStreamDestroy(stream_io), stream_io = nullptr;
        if (startEvent) cudaEventDestroy(startEvent), startEvent = nullptr;
        if (stopEvent) cudaEventDestroy(stopEvent), stopEvent = nullptr;
        if (memoryDoneEvent) cudaEventDestroy(memoryDoneEvent), memoryDoneEvent = nullptr;
        if (kernelDoneEvent) cudaEventDestroy(kernelDoneEvent), kernelDoneEvent = nullptr;
        if (resultsDoneEvent) cudaEventDestroy(resultsDoneEvent), resultsDoneEvent = nullptr;
        initialized = false;
    }
};

// Phase 5.2: Per-device mining thread function
// Each GPU runs this function in a separate thread
void miningThreadLoop(DeviceState* state, 
                      std::shared_ptr<MiningJobContext> jobContext,
                      void* stratumClient,
                      bool* isMultiGpuMode,
                      std::string* currentJobId,
                      uint32_t* currentEpoch) {
    try {
        CUDA_CHECK(cudaSetDevice(state->deviceId));
        
        LOG_INFO("[Mining Thread] Started on GPU #" + std::to_string(state->deviceId));
        state->threadRunning.store(true);
        
        std::shared_ptr<MiningJobContext> lastJobCtx = nullptr;
        
        while (!state->stopRequested.load()) {
            try {
                // Check for new job
                std::shared_ptr<MiningJobContext> currentCtx;
                {
                    std::lock_guard<std::mutex> lock(jobContext->jobMutex);
                    currentCtx = jobContext;
                }
                
                if (!currentCtx || !currentCtx->isValid.load()) {
                    // No valid job, wait briefly
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                // Execute search on this GPU with device-specific nonce range
                std::vector<Solution> solutions;
                
                // Use device-specific nonce offset and range
                const uint32_t maxSolutions = 16;
                
                // Reset solution counter
                uint32_t zero = 0;
                CUDA_CHECK(cudaMemcpyAsync(state->d_solutionCount, &zero, sizeof(uint32_t),
                                           cudaMemcpyHostToDevice, state->stream_memory));
                
                // Copy header, seedHash, and target to device
                CUDA_CHECK(cudaMemcpyAsync(state->d_header, currentCtx->headerHash.data(), 32,
                                           cudaMemcpyHostToDevice, state->stream_memory));
                CUDA_CHECK(cudaMemcpyAsync(state->d_seedHash, currentCtx->seedHash.data(), 32,
                                           cudaMemcpyHostToDevice, state->stream_memory));
                CUDA_CHECK(cudaMemcpyAsync(state->d_target, currentCtx->targetBE.data(), 32,
                                           cudaMemcpyHostToDevice, state->stream_memory));
                
                // Record memory sync event
                CUDA_CHECK(cudaEventRecord(state->memoryDoneEvent, state->stream_memory));
                CUDA_CHECK(cudaStreamWaitEvent(state->stream_compute, state->memoryDoneEvent));
                
                // Start timing
                CUDA_CHECK(cudaEventRecord(state->startEvent, state->stream_compute));
                
                // Get kernel settings
                static int useOptimized = -1;
                static uint32_t noncesPerThread = 1;
                if (useOptimized == -1) {
                    const char* env = std::getenv("OHMY_USE_OPTIMIZED_KERNEL");
                    useOptimized = (env && std::string(env) == "1") ? 1 : 0;
                    
                    const char* batchEnv = std::getenv("OHMY_NONCES_PER_THREAD");
                    if (batchEnv) {
                        int batch = std::atoi(batchEnv);
                        if (batch > 0 && batch <= 256) {
                            noncesPerThread = static_cast<uint32_t>(batch);
                        }
                    }
                }
                
                // Launch kernel with device-specific nonce range
                if (useOptimized) {
                    launch_ethash_search_optimized(
                        reinterpret_cast<const uint64_t*>(state->d_dag),
                        state->dagSize,
                        reinterpret_cast<const uint32_t*>(state->d_header),
                        reinterpret_cast<const uint32_t*>(state->d_seedHash),
                        reinterpret_cast<const uint8_t*>(state->d_target),
                        state->nonceOffset,
                        state->nonceRange,
                        noncesPerThread,
                        state->d_solutions,
                        state->d_solutionCount,
                        maxSolutions,
                        state->stream_compute
                    );
                } else {
                    launch_ethash_search(
                        reinterpret_cast<const uint64_t*>(state->d_dag),
                        state->dagSize,
                        reinterpret_cast<const uint32_t*>(state->d_header),
                        reinterpret_cast<const uint32_t*>(state->d_seedHash),
                        reinterpret_cast<const uint8_t*>(state->d_target),
                        state->nonceOffset,
                        state->nonceRange,
                        state->d_solutions,
                        state->d_solutionCount,
                        maxSolutions,
                        state->stream_compute
                    );
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
                        sol.jobId = currentCtx->jobId;
                        std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
                        std::memcpy(sol.result.data(), tmp[i].result, 32);
                        solutions.push_back(std::move(sol));
                    }
                    
                    LOG_INFO("[Mining Thread] GPU #" + std::to_string(state->deviceId) + 
                             " found " + std::to_string(numSolutions) + " solution(s)!");
                }
                
                // Create callback data with per-device info
                auto* cbData = new ohmy::cuda::ResultCallbackData();
                cbData->solutions = solutions;
                cbData->stratumClient = stratumClient;
                cbData->jobId = currentCtx->jobId;
                cbData->epoch = currentCtx->epoch;
                // Phase 5: Per-device tracking
                cbData->deviceId = state->deviceId;
                cbData->deviceHashesThisRound = state->nonceRange;
                
                // Compute timing for this round
                float roundMilliseconds = 0;
                CUDA_CHECK(cudaEventElapsedTime(&roundMilliseconds, state->startEvent, state->stopEvent));
                cbData->deviceTimeMilliseconds = roundMilliseconds;
                
                // Launch callback
                CUDA_CHECK(cudaLaunchHostFunc(state->stream_io,
                                              ohmy::cuda::processAndSubmitResultsCallback,
                                              cbData));
                
                // Record completion
                CUDA_CHECK(cudaEventRecord(state->resultsDoneEvent, state->stream_io));
                
                // Update statistics
                float milliseconds = 0;
                CUDA_CHECK(cudaEventElapsedTime(&milliseconds, state->startEvent, state->stopEvent));
                state->totalHashes += state->nonceRange;
                state->totalTime += milliseconds;
                
            } catch (const std::exception& e) {
                state->threadErrors++;
                LOG_ERROR("[Mining Thread] GPU #" + std::to_string(state->deviceId) + 
                         " error: " + e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        state->threadRunning.store(false);
        LOG_INFO("[Mining Thread] Stopped on GPU #" + std::to_string(state->deviceId));
        
    } catch (const std::exception& e) {
        state->threadRunning.store(false);
        state->threadErrors++;
        LOG_ERROR("[Mining Thread] GPU #" + std::to_string(state->deviceId) + 
                 " fatal error: " + e.what());
    }
}

class DeviceManager::Impl {
public:
    Impl() {
        int deviceCount = 0;
        CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
        LOG_INFO("Found " + std::to_string(deviceCount) + " CUDA device(s)");
        
        // Initialize member variables (Phase 4: single-GPU fallback)
        d_dag_ = nullptr;
        d_header_ = nullptr;
        d_seedHash_ = nullptr;
        d_solutions_ = nullptr;
        d_solutionCount_ = nullptr;
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
        useTexture_ = false;
        texDAG_ = 0;
        stratumClient_ = nullptr;
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
        
        // Allocate GPU memory for DAG
        dagSize_ = dagSize;
        CUDA_CHECK(cudaMalloc(&d_dag_, dagSize));
        
        // Copy DAG to GPU
        LOG_INFO("Copying DAG to GPU (" + std::to_string(dagSize / (1024*1024)) + " MB)");
        CUDA_CHECK(cudaMemcpy(d_dag_, dag, dagSize, cudaMemcpyHostToDevice));
        
        // Verify DAG was loaded (log first 32 bytes and item 8)
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
        
        // Allocate device memory for header and seedHash
        CUDA_CHECK(cudaMalloc(&d_header_, 32));    // 32 bytes for header
        CUDA_CHECK(cudaMalloc(&d_seedHash_, 32));  // 32 bytes for seedHash
        
    // Allocate solution buffers - now complete Solution structures (device POD)
        const uint32_t maxSolutions = 16;
    CUDA_CHECK(cudaMalloc(&d_solutions_, maxSolutions * sizeof(DeviceSolution)));
        CUDA_CHECK(cudaMalloc(&d_solutionCount_, sizeof(uint32_t)));

        // Allocate device memory for 256-bit target (big-endian)
        CUDA_CHECK(cudaMalloc(&d_target_, 32));
        
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
        
        LOG_INFO("Device " + std::to_string(deviceId) + " initialized successfully with 3-stream async pipeline (Phase 3)");
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
        const uint32_t maxSolutions = 16;
        
        // PHASE 3: Use 3-stream pipeline with event synchronization
        // Stream memory_: Async memcpy for data transfer
        // Stream compute_: Kernel execution  
        // Stream io_: Network/result processing
        
        // Reset solution counter asynchronously on stream_memory_
        uint32_t zero = 0;
        CUDA_CHECK(cudaMemcpyAsync(d_solutionCount_, &zero, sizeof(uint32_t), 
                                   cudaMemcpyHostToDevice, stream_memory_));
        
        // Copy header, seedHash and target to device asynchronously on stream_memory_
        // This allows stream_compute_ to prepare while memory transfers happen
        CUDA_CHECK(cudaMemcpyAsync(d_header_, headerHash.data(), 32, 
                                   cudaMemcpyHostToDevice, stream_memory_));
        CUDA_CHECK(cudaMemcpyAsync(d_seedHash_, seedHash.data(), 32, 
                                   cudaMemcpyHostToDevice, stream_memory_));
        CUDA_CHECK(cudaMemcpyAsync(d_target_, targetBE, 32, 
                                   cudaMemcpyHostToDevice, stream_memory_));
        
        // Record event to signal completion of memory transfers on stream_memory_
        CUDA_CHECK(cudaEventRecord(memoryDoneEvent_, stream_memory_));
        
        // Make stream_compute_ wait for memory transfers to complete before launching kernel
        // This ensures data is ready, but CPU thread continues immediately (non-blocking)
        CUDA_CHECK(cudaStreamWaitEvent(stream_compute_, memoryDoneEvent_));
        
        // Start timing on compute stream AFTER memory data is guaranteed to be ready
        CUDA_CHECK(cudaEventRecord(startEvent_, stream_compute_));
        
        // Check for optimized kernel flag (env var OHMY_USE_OPTIMIZED_KERNEL=1)
        static int useOptimized = -1;
        static uint32_t noncesPerThread = 1;  // Default: 1 nonce/thread (OPTIMAL: +2.7% vs prev default, empirically tuned)
        if (useOptimized == -1) {
            const char* env = std::getenv("OHMY_USE_OPTIMIZED_KERNEL");
            useOptimized = (env && std::string(env) == "1") ? 1 : 0;
            
            // Allow custom noncesPerThread via env var for experimentation
            // Empirical results show degradation with larger values:
            // NONCES=1 (8.02 MH/s) > 2 (7.99) > 3 (7.91) > 4 (7.81) > 8 (7.61) > 16+ (degraded)
            const char* batchEnv = std::getenv("OHMY_NONCES_PER_THREAD");
            if (batchEnv) {
                int batch = std::atoi(batchEnv);
                if (batch > 0 && batch <= 256) {
                    noncesPerThread = static_cast<uint32_t>(batch);
                }
            }
            
            if (useOptimized) {
                LOG_INFO("Using optimized kernel with batching (noncesPerThread=" + 
                         std::to_string(noncesPerThread) + ")");
            }
        }
        
        if (useOptimized) {
            // Use optimized kernel with advanced caching and high batching
            // Kernel executes on stream_compute_
            launch_ethash_search_optimized(
                reinterpret_cast<const uint64_t*>(d_dag_),
                dagSize_,
                reinterpret_cast<const uint32_t*>(d_header_),
                reinterpret_cast<const uint32_t*>(d_seedHash_),
                reinterpret_cast<const uint8_t*>(d_target_),
                startNonce,
                count,
                noncesPerThread,
                d_solutions_,
                d_solutionCount_,
                maxSolutions,
                stream_compute_
            );
        } else {
            // Use base kernel (1 nonce per thread)
            // Kernel executes on stream_compute_
            launch_ethash_search(
                reinterpret_cast<const uint64_t*>(d_dag_),
                dagSize_,
                reinterpret_cast<const uint32_t*>(d_header_),
                reinterpret_cast<const uint32_t*>(d_seedHash_),
                reinterpret_cast<const uint8_t*>(d_target_),
                startNonce,
                count,
                d_solutions_,
                d_solutionCount_,
                maxSolutions,
                stream_compute_
            );
        }
        
        // Record kernel completion event on stream_compute_
        // This signals that all GPU work (kernel) is complete
        CUDA_CHECK(cudaEventRecord(kernelDoneEvent_, stream_compute_));
        
        // Make stream_io_ wait for kernel completion before processing results
        // This ensures kernelDoneEvent is recorded before sync
        CUDA_CHECK(cudaStreamWaitEvent(stream_io_, kernelDoneEvent_));
        
        // Stop timing on compute stream
        CUDA_CHECK(cudaEventRecord(stopEvent_, stream_compute_));
        
        // PHASE 4: Read results synchronously BEFORE launching callback
        // (Callback thread cannot access CUDA device memory)
        
        // Get solution count from device
        uint32_t numSolutions = 0;
        CUDA_CHECK(cudaMemcpy(&numSolutions, d_solutionCount_, sizeof(uint32_t), 
                              cudaMemcpyDeviceToHost));
        
        if (numSolutions > 0) {
            // Limit to maxSolutions
            numSolutions = std::min(numSolutions, maxSolutions);
            
            // Copy complete solutions from device POD to host Solution objects
            std::vector<DeviceSolution> tmp(numSolutions);
            CUDA_CHECK(cudaMemcpy(tmp.data(), d_solutions_,
                                 numSolutions * sizeof(DeviceSolution), cudaMemcpyDeviceToHost));
            solutions.clear();
            solutions.reserve(numSolutions);
            for (uint32_t i = 0; i < numSolutions; ++i) {
                Solution sol;
                sol.nonce = tmp[i].nonce;
                sol.jobId = currentJobId_;  // Set job ID before passing to callback
                std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
                std::memcpy(sol.result.data(),  tmp[i].result,  32);
                solutions.push_back(std::move(sol));
            }
            
            LOG_INFO("Found " + std::to_string(numSolutions) + " solution(s)!");
        }
        
        // Phase 4.3: Create callback data with already-read solutions
        // (No CUDA memory access in callback, all data already copied to host)
        auto* cbData = new ohmy::cuda::ResultCallbackData();
        cbData->solutions = solutions;              // Pass host-side solutions
        cbData->stratumClient = stratumClient_;     // StratumClient for pool submission
        cbData->jobId = currentJobId_;              // Job ID from mining context
        cbData->epoch = currentEpoch_;              // Epoch from mining context
        // Phase 5: Single-GPU compatibility (device 0)
        cbData->deviceId = 0;                       // Device 0 for single-GPU mode
        cbData->deviceHashesThisRound = count;      // Use count parameter for hashes
        
        // Compute timing for this round
        float roundMilliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&roundMilliseconds, startEvent_, stopEvent_));
        cbData->deviceTimeMilliseconds = roundMilliseconds;
        
        // Launch callback on stream_io_ (non-blocking, callback runs async)
        // Callback will submit solutions to pool without accessing GPU
        CUDA_CHECK(cudaLaunchHostFunc(stream_io_, 
                                      ohmy::cuda::processAndSubmitResultsCallback,
                                      cbData));
        
        // Record results completion event on stream_io_
        // This signals that all result transfers and processing are complete
        CUDA_CHECK(cudaEventRecord(resultsDoneEvent_, stream_io_));
        
        // Calculate elapsed time
        float milliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&milliseconds, startEvent_, stopEvent_));
        
        // Update statistics
        totalHashes_ += count;
        totalTime_ += milliseconds;
        
        return numSolutions;
    }

    uint64_t getHashRate(int deviceId) const {
        // Calculate average hashrate in H/s (hashes per second)
        if (totalTime_ <= 0.0f) {
            return 0;
        }
        
        // totalTime_ is in milliseconds, convert to seconds
        float seconds = totalTime_ / 1000.0f;
        uint64_t hashrate = static_cast<uint64_t>(totalHashes_ / seconds);
        
        return hashrate;
    }

    /**
     * @brief Phase 4: Set StratumClient for async result submission
     */
    void setResultCallback(void* stratumClient) {
        stratumClient_ = stratumClient;
        LOG_DEBUG("ResultCallback: StratumClient registered");
    }

    /**
     * @brief Phase 5: Set mining job context (jobId and epoch)
     */
    void setMiningJobContext(const std::string& jobId, uint32_t epoch) {
        currentJobId_ = jobId;
        currentEpoch_ = epoch;
        LOG_DEBUG("ResultCallback: Job context set - jobId=" + jobId.substr(0, 8) + "..., epoch=" + std::to_string(epoch));
    }

    /**
     * @brief Phase 5: Initialize all available CUDA devices
     */
    int initializeAllDevices(const void* dag, size_t dagSize) {
        int deviceCount = 0;
        CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
        
        if (deviceCount <= 0) {
            LOG_ERROR("No CUDA devices found!");
            return 0;
        }
        
        LOG_INFO("Initializing all " + std::to_string(deviceCount) + " CUDA devices for mining");
        
        isMultiGpuMode_ = true;
        deviceStates_.clear();
        
        int successCount = 0;
        for (int i = 0; i < deviceCount; ++i) {
            try {
                if (initDeviceWithMultiGpu(i, deviceCount, dag, dagSize)) {
                    successCount++;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize device " + std::to_string(i) + ": " + e.what());
            }
        }
        
        LOG_INFO("Successfully initialized " + std::to_string(successCount) + " device(s)");
        return successCount;
    }

    /**
     * @brief Phase 5: Initialize specific device with multi-GPU nonce distribution
     */
    bool initDeviceWithMultiGpu(int deviceId, int devicesTotal, 
                                const void* dag, size_t dagSize) {
        CUDA_CHECK(cudaSetDevice(deviceId));
        
        LOG_INFO("Initializing device " + std::to_string(deviceId) + " (part of " + 
                 std::to_string(devicesTotal) + " GPU setup)");
        
        // Create device state via map
        auto state = std::make_unique<DeviceState>();
        state->deviceId = deviceId;
        state->dagSize = dagSize;
        
        try {
            // Allocate GPU memory for DAG
            CUDA_CHECK(cudaMalloc(&state->d_dag, dagSize));
            
            // Copy DAG to GPU
            LOG_INFO("Copying DAG to GPU " + std::to_string(deviceId) + " (" + 
                     std::to_string(dagSize / (1024*1024)) + " MB)");
            CUDA_CHECK(cudaMemcpy(state->d_dag, dag, dagSize, cudaMemcpyHostToDevice));
            
            // Allocate device memory for headers and hashes
            CUDA_CHECK(cudaMalloc(&state->d_header, 32));
            CUDA_CHECK(cudaMalloc(&state->d_seedHash, 32));
            
            // Allocate solution buffers
            const uint32_t maxSolutions = 16;
            CUDA_CHECK(cudaMalloc(&state->d_solutions, maxSolutions * sizeof(DeviceSolution)));
            CUDA_CHECK(cudaMalloc(&state->d_solutionCount, sizeof(uint32_t)));
            
            // Allocate device memory for target
            CUDA_CHECK(cudaMalloc(&state->d_target, 32));
            
            // Create THREE CUDA streams per device for 3-stream pipeline
            CUDA_CHECK(cudaStreamCreate(&state->stream_compute));
            CUDA_CHECK(cudaStreamCreate(&state->stream_memory));
            CUDA_CHECK(cudaStreamCreate(&state->stream_io));
            
            // Create events for synchronization
            CUDA_CHECK(cudaEventCreate(&state->startEvent));
            CUDA_CHECK(cudaEventCreate(&state->stopEvent));
            CUDA_CHECK(cudaEventCreate(&state->memoryDoneEvent));
            CUDA_CHECK(cudaEventCreate(&state->kernelDoneEvent));
            CUDA_CHECK(cudaEventCreate(&state->resultsDoneEvent));
            
            // Calculate nonce range for this device (partition nonce space)
            // Each device gets (2^32 / devicesTotal) nonces
            uint64_t rangeSize = (1ULL << 32) / devicesTotal;
            state->nonceOffset = static_cast<uint32_t>(deviceId * rangeSize);
            state->nonceRange = static_cast<uint32_t>(rangeSize);
            
            state->initialized = true;
            
            // Insert into map
            deviceStates_[deviceId] = std::move(state);
            
            LOG_INFO("Device " + std::to_string(deviceId) + " initialized successfully");
            LOG_INFO("  Nonce range: [0x" + std::to_string(deviceStates_[deviceId]->nonceOffset) + 
                     ", 0x" + std::to_string(deviceStates_[deviceId]->nonceOffset + 
                     deviceStates_[deviceId]->nonceRange) + ")");
            
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during device initialization: " + std::string(e.what()));
            state->cleanup();
            return false;
        }
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

    /**
     * @brief Phase 5: Get total hashrate from all devices
     */
    uint64_t getTotalHashRate() const {
        uint64_t total = 0;
        if (isMultiGpuMode_) {
            for (const auto& entry : deviceStates_) {
                if (entry.second && entry.second->initialized && entry.second->totalTime > 0.0f) {
                    float seconds = entry.second->totalTime / 1000.0f;
                    total += static_cast<uint64_t>(entry.second->totalHashes / seconds);
                }
            }
        } else {
            total = getHashRate(0);  // Fallback to single-GPU
        }
        return total;
    }

    /**
     * @brief Phase 5: Get per-device hashrates
     */
    std::vector<uint64_t> getAllHashRates() const {
        std::vector<uint64_t> rates;
        if (isMultiGpuMode_) {
            for (const auto& entry : deviceStates_) {
                if (entry.second && entry.second->initialized && entry.second->totalTime > 0.0f) {
                    float seconds = entry.second->totalTime / 1000.0f;
                    rates.push_back(static_cast<uint64_t>(entry.second->totalHashes / seconds));
                } else {
                    rates.push_back(0);
                }
            }
        } else {
            rates.push_back(getHashRate(0));  // Fallback
        }
        return rates;
    }

    /**
     * @brief Phase 5: Get per-device statistics
     */
    std::vector<std::string> getDeviceStatistics(int deviceId = -1) const {
        std::vector<std::string> stats;
        
        if (deviceId >= 0) {
            // Get stats for specific device
            auto it = deviceStates_.find(deviceId);
            if (it != deviceStates_.end() && it->second && it->second->initialized) {
                // Build statistics string for this device
                auto* state = it->second.get();
                float seconds = state->totalTime > 0.0f ? state->totalTime / 1000.0f : 0.0f;
                double hashrate = seconds > 0.0 ? (double)state->totalHashes / seconds / 1e6 : 0.0;
                
                std::ostringstream oss;
                oss << "GPU#" << deviceId << ": "
                    << "Hashes=" << state->totalHashes << " "
                    << "Time=" << state->totalTime << "ms "
                    << "Rate=" << std::fixed << std::setprecision(2) << hashrate << " MH/s";
                stats.push_back(oss.str());
            }
        } else {
            // Get stats for all devices
            for (const auto& entry : deviceStates_) {
                if (entry.second && entry.second->initialized) {
                    auto* state = entry.second.get();
                    float seconds = state->totalTime > 0.0f ? state->totalTime / 1000.0f : 0.0f;
                    double hashrate = seconds > 0.0 ? (double)state->totalHashes / seconds / 1e6 : 0.0;
                    
                    std::ostringstream oss;
                    oss << "GPU#" << entry.first << ": "
                        << "Hashes=" << state->totalHashes << " "
                        << "Time=" << state->totalTime << "ms "
                        << "Rate=" << std::fixed << std::setprecision(2) << hashrate << " MH/s";
                    stats.push_back(oss.str());
                }
            }
        }
        
        return stats;
    }

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
        
        // Get kernel settings
        static int useOptimized = -1;
        static uint32_t noncesPerThread = 1;
        if (useOptimized == -1) {
            const char* env = std::getenv("OHMY_USE_OPTIMIZED_KERNEL");
            useOptimized = (env && std::string(env) == "1") ? 1 : 0;
            
            const char* batchEnv = std::getenv("OHMY_NONCES_PER_THREAD");
            if (batchEnv) {
                int batch = std::atoi(batchEnv);
                if (batch > 0 && batch <= 256) {
                    noncesPerThread = static_cast<uint32_t>(batch);
                }
            }
        }
        
        // Launch kernel with device-specific nonce range
        // Use device's nonce offset as starting point, range as search count
        if (useOptimized) {
            launch_ethash_search_optimized(
                reinterpret_cast<const uint64_t*>(state->d_dag),
                state->dagSize,
                reinterpret_cast<const uint32_t*>(state->d_header),
                reinterpret_cast<const uint32_t*>(state->d_seedHash),
                reinterpret_cast<const uint8_t*>(state->d_target),
                state->nonceOffset,  // Device-specific nonce offset
                state->nonceRange,   // Device-specific range
                noncesPerThread,
                state->d_solutions,
                state->d_solutionCount,
                maxSolutions,
                state->stream_compute
            );
        } else {
            launch_ethash_search(
                reinterpret_cast<const uint64_t*>(state->d_dag),
                state->dagSize,
                reinterpret_cast<const uint32_t*>(state->d_header),
                reinterpret_cast<const uint32_t*>(state->d_seedHash),
                reinterpret_cast<const uint8_t*>(state->d_target),
                state->nonceOffset,  // Device-specific nonce offset
                state->nonceRange,   // Device-specific range
                state->d_solutions,
                state->d_solutionCount,
                maxSolutions,
                state->stream_compute
            );
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
        
        // Create callback data (same as single-GPU)
        auto* cbData = new ohmy::cuda::ResultCallbackData();
        cbData->solutions = solutions;
        cbData->stratumClient = stratumClient_;
        cbData->jobId = currentJobId_;
        cbData->epoch = currentEpoch_;
        // Phase 5: Per-device tracking
        cbData->deviceId = deviceId;
        cbData->deviceHashesThisRound = state->nonceRange;
        
        // Compute timing for this round
        float roundMilliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&roundMilliseconds, state->startEvent, state->stopEvent));
        cbData->deviceTimeMilliseconds = roundMilliseconds;
        
        // Launch callback
        CUDA_CHECK(cudaLaunchHostFunc(state->stream_io,
                                      ohmy::cuda::processAndSubmitResultsCallback,
                                      cbData));
        
        // Record completion
        CUDA_CHECK(cudaEventRecord(state->resultsDoneEvent, state->stream_io));
        
        // Update statistics
        float milliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&milliseconds, state->startEvent, state->stopEvent));
        state->totalHashes += state->nonceRange;
        state->totalTime += milliseconds;
        
        return numSolutions;
    }

    /**
     * @brief Phase 5: Start mining on all devices (placeholder for threading)
     */
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
                miningThreadLoop,
                state.get(),
                jobContext_,
                stratumClient_,
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
        // Destroy texture object if it was created
        if (texDAG_ != 0) {
            cudaDestroyTextureObject(texDAG_);
            texDAG_ = 0;
        }
        
        if (d_dag_) {
            cudaFree(d_dag_);
            d_dag_ = nullptr;
        }
        if (d_header_) {
            cudaFree(d_header_);
            d_header_ = nullptr;
        }
        if (d_seedHash_) {
            cudaFree(d_seedHash_);
            d_seedHash_ = nullptr;
        }
        if (d_solutions_) {
            cudaFree(d_solutions_);
            d_solutions_ = nullptr;
        }
        if (d_solutionCount_) {
            cudaFree(d_solutionCount_);
            d_solutionCount_ = nullptr;
        }
        if (d_target_) {
            cudaFree(d_target_);
            d_target_ = nullptr;
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
    void* d_header_;
    void* d_seedHash_;       // Seed hash from mining job
    DeviceSolution* d_solutions_; // Device-side POD solutions
    uint32_t* d_solutionCount_;
    void* d_target_;
    size_t dagSize_;
    
    // THREE CUDA streams for overlapping compute, memory, and I/O operations
    // stream_compute_: GPU kernel execution (compute-bound)
    // stream_memory_: Host<->Device memory transfers (memory-bound)
    // stream_io_: Network I/O and non-blocking operations (NEW for Phase 3)
    cudaStream_t stream_compute_;
    cudaStream_t stream_memory_;
    cudaStream_t stream_io_;  // Phase 3: Added for true 3-stream pipeline
    
    // Optimization flags and resources
    bool useTexture_;               // Whether texture memory is enabled
    cudaTextureObject_t texDAG_;    // Texture object for DAG access
    
    // Timing and statistics
    cudaEvent_t startEvent_;
    cudaEvent_t stopEvent_;
    
    // Phase 3: Stream synchronization events for 3-stream pipeline
    cudaEvent_t memoryDoneEvent_;      // Signals completion of memory transfers
    cudaEvent_t kernelDoneEvent_;      // Signals completion of kernel execution
    cudaEvent_t resultsDoneEvent_;     // Signals completion of result transfers
    
    uint64_t totalHashes_;
    float totalTime_;  // milliseconds
    
    // Phase 4: Callback context
    void* stratumClient_;           // StratumClient pointer for pool submission
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

uint64_t DeviceManager::getHashRate(int deviceId) const {
    return pImpl_->getHashRate(deviceId);
}

void DeviceManager::setResultCallback(void* stratumClient) {
    pImpl_->setResultCallback(stratumClient);
}

void DeviceManager::setMiningJobContext(const std::string& jobId, uint32_t epoch) {
    pImpl_->setMiningJobContext(jobId, epoch);
}

// Phase 5: Multi-GPU wrapper methods
int DeviceManager::initializeAllDevices(const void* dag, size_t dagSize) {
    return pImpl_->initializeAllDevices(dag, dagSize);
}

bool DeviceManager::initDeviceWithMultiGpu(int deviceId, int devicesTotal,
                                           const void* dag, size_t dagSize) {
    return pImpl_->initDeviceWithMultiGpu(deviceId, devicesTotal, dag, dagSize);
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

} // namespace cuda
} // namespace ohmy
