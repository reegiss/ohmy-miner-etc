#include "ohmy/device_manager.hpp"
#include "ohmy/logger.hpp"
#include <cuda_runtime.h>
#include <stdexcept>
#include <vector>

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

// Forward declaration of kernel launch function
extern "C" void launch_ethash_search(
    const uint64_t* d_dag,
    uint64_t dagSize,
    const uint32_t* d_header,
    uint64_t target,
    uint64_t startNonce,
    uint64_t searchCount,
    uint64_t* d_solutions,
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
        d_solutions_ = nullptr;
        d_solutionCount_ = nullptr;
        dagSize_ = 0;
        stream_ = nullptr;
        startEvent_ = nullptr;
        stopEvent_ = nullptr;
        totalHashes_ = 0;
        totalTime_ = 0.0f;
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
        
        // Allocate device memory for header
        CUDA_CHECK(cudaMalloc(&d_header_, 32));  // 32 bytes for header
        
        // Allocate solution buffers
        const uint32_t maxSolutions = 16;
        CUDA_CHECK(cudaMalloc(&d_solutions_, maxSolutions * sizeof(uint64_t)));
        CUDA_CHECK(cudaMalloc(&d_solutionCount_, sizeof(uint32_t)));
        
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
        uint64_t target,
        uint64_t startNonce,
        uint64_t count,
        std::vector<Solution>& solutions
    ) {
        const uint32_t maxSolutions = 16;
        
        // Reset solution counter
        uint32_t zero = 0;
        CUDA_CHECK(cudaMemcpy(d_solutionCount_, &zero, sizeof(uint32_t), cudaMemcpyHostToDevice));
        
        // Copy header to device
        CUDA_CHECK(cudaMemcpy(d_header_, headerHash.data(), 32, cudaMemcpyHostToDevice));
        
        // Start timing
        CUDA_CHECK(cudaEventRecord(startEvent_, stream_));
        
        // Launch search kernel
        launch_ethash_search(
            reinterpret_cast<const uint64_t*>(d_dag_),
            dagSize_,
            reinterpret_cast<const uint32_t*>(d_header_),
            target,
            startNonce,
            count,
            d_solutions_,
            d_solutionCount_,
            maxSolutions,
            stream_
        );
        
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
        LOG_INFO("Searched " + std::to_string(count) + " nonces in " + 
                 std::to_string(milliseconds) + " ms (" + 
                 std::to_string(currentHashrate) + " MH/s)");
        
        // Get solution count
        uint32_t numSolutions = 0;
        CUDA_CHECK(cudaMemcpy(&numSolutions, d_solutionCount_, sizeof(uint32_t), cudaMemcpyDeviceToHost));
        
        if (numSolutions > 0) {
            // Limit to maxSolutions
            numSolutions = std::min(numSolutions, maxSolutions);
            
            // Copy solutions from device
            std::vector<uint64_t> nonces(numSolutions);
            CUDA_CHECK(cudaMemcpy(nonces.data(), d_solutions_, 
                                 numSolutions * sizeof(uint64_t), cudaMemcpyDeviceToHost));
            
            // Convert to Solution structs
            for (uint32_t i = 0; i < numSolutions; ++i) {
                Solution sol;
                sol.nonce = nonces[i];
                // TODO: Calculate actual mixHash and result
                sol.mixHash.fill(0);
                sol.result.fill(0);
                solutions.push_back(sol);
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
        if (d_dag_) {
            cudaFree(d_dag_);
            d_dag_ = nullptr;
        }
        if (d_header_) {
            cudaFree(d_header_);
            d_header_ = nullptr;
        }
        if (d_solutions_) {
            cudaFree(d_solutions_);
            d_solutions_ = nullptr;
        }
        if (d_solutionCount_) {
            cudaFree(d_solutionCount_);
            d_solutionCount_ = nullptr;
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
    uint64_t* d_solutions_;
    uint32_t* d_solutionCount_;
    size_t dagSize_;
    cudaStream_t stream_;
    
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
    uint64_t target,
    uint64_t startNonce,
    uint64_t count,
    std::vector<Solution>& solutions
) {
    return pImpl_->search(headerHash, target, startNonce, count, solutions);
}

uint64_t DeviceManager::getHashRate(int deviceId) const {
    return pImpl_->getHashRate(deviceId);
}

} // namespace cuda
} // namespace ohmy
