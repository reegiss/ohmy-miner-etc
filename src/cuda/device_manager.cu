#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include "result_callback.hpp"
#include <cuda_runtime.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

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

class DeviceManager::Impl {
public:
    Impl() {
        int deviceCount = 0;
        CUDA_CHECK(cudaGetDeviceCount(&deviceCount));
        LOG_INFO("Found " + std::to_string(deviceCount) + " CUDA device(s)");
        
        // Initialize member variables
        d_dag_ = nullptr;
        d_header_ = nullptr;
        d_seedHash_ = nullptr;
        d_solutions_ = nullptr;
        d_solutionCount_ = nullptr;
        dagSize_ = 0;
        stream_compute_ = nullptr;
        stream_memory_ = nullptr;
        stream_io_ = nullptr;  // Phase 3: Initialize new stream
        startEvent_ = nullptr;
        stopEvent_ = nullptr;
        memoryDoneEvent_ = nullptr;    // Phase 3: Initialize sync events
        kernelDoneEvent_ = nullptr;
        resultsDoneEvent_ = nullptr;
        totalHashes_ = 0;
        totalTime_ = 0.0f;
        useTexture_ = false;
        texDAG_ = 0;
        stratumClient_ = nullptr;
        currentJobId_ = "";
        currentEpoch_ = 0;
    }

    ~Impl() {
        cleanup();
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
        static int useTexture = -1;
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
     * @brief Phase 4: Set mining job context (jobId and epoch)
     */
    void setMiningJobContext(const std::string& jobId, uint32_t epoch) {
        currentJobId_ = jobId;
        currentEpoch_ = epoch;
        LOG_DEBUG("ResultCallback: Job context set - jobId=" + jobId.substr(0, 8) + "..., epoch=" + std::to_string(epoch));
    }

private:
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

} // namespace cuda
} // namespace ohmy
