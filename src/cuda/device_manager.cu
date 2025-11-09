#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include <cuda_runtime.h>
#include <cstring>
#include <stdexcept>
#include <vector>
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
        stream_ = nullptr;
        startEvent_ = nullptr;
        stopEvent_ = nullptr;
        totalHashes_ = 0;
        totalTime_ = 0.0f;
        useTexture_ = false;
        texDAG_ = 0;
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
        
        // Create CUDA stream for async operations
        CUDA_CHECK(cudaStreamCreate(&stream_));
        
        // Create events for timing
        CUDA_CHECK(cudaEventCreate(&startEvent_));
        CUDA_CHECK(cudaEventCreate(&stopEvent_));
        
        LOG_INFO("Device " + std::to_string(deviceId) + " initialized successfully");
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
        
        // Reset solution counter
        uint32_t zero = 0;
        CUDA_CHECK(cudaMemcpy(d_solutionCount_, &zero, sizeof(uint32_t), cudaMemcpyHostToDevice));
        
        // Copy header, seedHash and target to device
        CUDA_CHECK(cudaMemcpy(d_header_, headerHash.data(), 32, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_seedHash_, seedHash.data(), 32, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_target_, targetBE, 32, cudaMemcpyHostToDevice));
        
        // Start timing
        CUDA_CHECK(cudaEventRecord(startEvent_, stream_));
        
        // Check for optimized kernel flag (env var OHMY_USE_OPTIMIZED_KERNEL=1)
        static int useOptimized = -1;
        static int useTexture = -1;
        static uint32_t noncesPerThread = 4;  // Default: 4 nonces/thread (best balance: +17%)
        if (useOptimized == -1) {
            const char* env = std::getenv("OHMY_USE_OPTIMIZED_KERNEL");
            useOptimized = (env && std::string(env) == "1") ? 1 : 0;
            
            // Allow custom noncesPerThread via env var for experimentation
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
                stream_
            );
        } else {
            // Use base kernel (1 nonce per thread)
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
                stream_
            );
        }
        
        // Stop timing
        CUDA_CHECK(cudaEventRecord(stopEvent_, stream_));
        
        // Wait for kernel completion
        CUDA_CHECK(cudaStreamSynchronize(stream_));
        
        // Calculate elapsed time
        float milliseconds = 0;
        CUDA_CHECK(cudaEventElapsedTime(&milliseconds, startEvent_, stopEvent_));
        
        // Update statistics
        totalHashes_ += count;
        totalTime_ += milliseconds;
        
        // Calculate current hashrate (MH/s)
        float currentHashrate = (count / 1000000.0f) / (milliseconds / 1000.0f);
        // LOG_INFO removed to reduce log frequency
        
        // Get solution count
        uint32_t numSolutions = 0;
        CUDA_CHECK(cudaMemcpy(&numSolutions, d_solutionCount_, sizeof(uint32_t), cudaMemcpyDeviceToHost));
        
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
                std::memcpy(sol.mixHash.data(), tmp[i].mixHash, 32);
                std::memcpy(sol.result.data(),  tmp[i].result,  32);
                solutions.push_back(std::move(sol));
            }
            
            LOG_INFO("Found " + std::to_string(numSolutions) + " solution(s)!");
        }
        
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
        if (stream_) {
            cudaStreamDestroy(stream_);
            stream_ = nullptr;
        }
        if (startEvent_) {
            cudaEventDestroy(startEvent_);
            startEvent_ = nullptr;
        }
        if (stopEvent_) {
            cudaEventDestroy(stopEvent_);
            stopEvent_ = nullptr;
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
    cudaStream_t stream_;
    
    // Optimization flags and resources
    bool useTexture_;               // Whether texture memory is enabled
    cudaTextureObject_t texDAG_;    // Texture object for DAG access
    
    // Timing and statistics
    cudaEvent_t startEvent_;
    cudaEvent_t stopEvent_;
    uint64_t totalHashes_;
    float totalTime_;  // milliseconds
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

} // namespace cuda
} // namespace ohmy
