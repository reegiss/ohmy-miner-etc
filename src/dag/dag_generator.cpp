#include "ohmy/dag_generator.hpp"
#include "ohmy/ethash.hpp"
#include "ohmy/logger.hpp"
#include "ohmy/device_manager.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

// Define CUDA_CHECK macro if not already defined
#ifndef CUDA_CHECK
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            LOG_ERROR("CUDA error: " + std::string(cudaGetErrorString(err))); \
            throw std::runtime_error("CUDA error"); \
        } \
    } while(0)
#endif

namespace ohmy {
namespace dag {

class DagGenerator::Impl {
public:
    Impl() : currentEpoch_(0), dagData_(nullptr), dagSize_(0), d_dag_(nullptr) {}

    ~Impl() {
        cleanup();
    }

    const void* generate(uint32_t epoch, bool useGpu) {
        if (currentEpoch_ == epoch && dagData_ != nullptr) {
            return dagData_;
        }

        LOG_INFO("Generating DAG for epoch " + std::to_string(epoch) + "...");

        // GPU-FIRST: No disk cache - DAG is GPU-resident only
        // Fast regeneration with GPU means disk I/O is slower than just regenerating
        // This also avoids 4GB RAM spike from loading cached DAG

        // Calculate sizes
        uint32_t cacheSize = Ethash::getCacheSize(epoch);
        dagSize_ = Ethash::getDatasetSize(epoch);
        uint32_t numItems = dagSize_ / 64; // Each item is 64 bytes
        
        // Ensure we always generate full DAG in production now (removed DEBUG_PARTIAL_DAG)
        
        LOG_INFO("  AFTER ASSIGNMENT: dagSize_ = " + std::to_string(dagSize_));
        LOG_INFO("  Cache size: " + std::to_string(cacheSize / 1024 / 1024) + " MB");
        LOG_INFO("  DAG size: " + std::to_string(dagSize_ / 1024 / 1024) + " MB (" + std::to_string(dagSize_) + " bytes)");
        LOG_INFO("  Items: " + std::to_string(numItems));

        // Generate cache
        LOG_INFO("  Generating cache...");
        auto cache = Ethash::calculateCache(epoch);
        LOG_INFO("  ✓ Cache generated (" + std::to_string(cache.size()) + " items)");

        // For GPU-first generation we avoid allocating the full DAG in host RAM.
        // Many DAG implementations allocate a large host buffer which causes a giant
        // virtual memory footprint and high memory pressure. Instead, when `useGpu`
        // is true we keep the DAG resident in GPU memory only and use a small
        // placeholder in host memory for API compatibility.

        // Free previous host placeholder if present
        if (dagData_ != nullptr) {
            std::free(dagData_);
            dagData_ = nullptr;
        }

        // Generate DAG items
        LOG_INFO("  Generating DAG items...");
        auto startTime = std::chrono::steady_clock::now();
        
        if (!useGpu) {
            return nullptr;
        }
        
        LOG_INFO("  Using GPU for DAG generation");
        
        // Allocate GPU memory for cache
        void* d_cache = nullptr;
        void* d_dag = nullptr;
        
        size_t cacheBytes = cache.size() * sizeof(hash64_t);
        cudaError_t err = cudaMalloc(&d_cache, cacheBytes);
        if (err != cudaSuccess) {
            LOG_ERROR("  Failed to allocate GPU cache memory: " + std::string(cudaGetErrorString(err)));
            return nullptr;
        }
        
        // Allocate GPU memory for DAG
        // Log GPU memory usage before and after DAG allocation
        size_t freeMem, totalMem;
        CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
        LOG_INFO("GPU memory before DAG allocation: " + std::to_string(freeMem / (1024 * 1024)) + " MB free");

        err = cudaMalloc(&d_dag, dagSize_);
        if (err != cudaSuccess) {
            LOG_ERROR("  Failed to allocate GPU DAG memory: " + std::string(cudaGetErrorString(err)));
            LOG_ERROR("  GPU memory exhausted - need at least " + std::to_string(dagSize_ / (1024*1024)) + " MB free VRAM");
            cudaFree(d_cache);
            return nullptr;
        }

        CUDA_CHECK(cudaMemGetInfo(&freeMem, &totalMem));
        LOG_INFO("GPU memory after DAG allocation: " + std::to_string(freeMem / (1024 * 1024)) + " MB free");
        
        // Copy cache to GPU
        err = cudaMemcpy(d_cache, cache.data(), cacheBytes, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) {
            LOG_ERROR("  Failed to copy cache to GPU: " + std::string(cudaGetErrorString(err)));
            cudaFree(d_cache);
            cudaFree(d_dag);
            return nullptr;
        } else {
            LOG_INFO("  ✓ Cache copied to GPU (" + std::to_string(cacheBytes / (1024*1024)) + " MB)");
            // Free the host-side cache now that it's on the device to avoid
            // holding tens of MB in RAM during the long GPU DAG generation.
            cache.clear();
            cache.shrink_to_fit();
            LOG_DEBUG("  Host cache cleared to minimize RAM usage");
        }
        
        // Generate DAG on GPU
        try {
            const uint32_t reportInterval = numItems / 20;
            std::atomic<bool> completed{false};
            
            // Progress monitor thread
            std::thread monitor([&completed, reportInterval, numItems]() {
                for (uint32_t pct = 5; pct <= 95 && !completed.load(); pct += 5) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (!completed.load()) {
                        LOG_INFO("    Progress: ~" + std::to_string(pct) + "%");
                    }
                }
            });
            
            ohmy::cuda::generateDagGpu(static_cast<const uint64_t*>(d_cache), d_dag, cache.size(), numItems);
            LOG_INFO("generateDagGpu completed successfully");
            completed.store(true);
            monitor.join();
            
            LOG_INFO("  ✓ DAG generated on GPU");
            
            // GPU-FIRST: Keep DAG in GPU memory only. Do NOT allocate the
            // full DAG on the host to avoid a large memory spike. Instead
            // create a small placeholder for API calls that expect a
            // non-null host pointer.
            LOG_INFO("  DAG remains in GPU memory (zero-copy architecture)");

            // Allocate minimal placeholder (API compatibility)
            dagData_ = std::malloc(128);
            if (dagData_) {
                std::memset(dagData_, 0x42, 128);  // GPU-resident marker
            }
            
            // Cleanup GPU cache only (DAG stays allocated for mining)
            cudaFree(d_cache);
            
            // Store GPU pointer for zero-copy access
            d_dag_ = d_dag;
            
            // IMPORTANT: d_dag is NOT freed - it stays in GPU memory
        } catch (const std::exception& e) {
            LOG_ERROR("  Exception during GPU generation: " + std::string(e.what()));
            cudaFree(d_cache);
            cudaFree(d_dag);
            return nullptr;
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
        
        LOG_INFO("  ✓ DAG generated in " + std::to_string(duration.count()) + "s (GPU-only, zero CPU usage)");

        currentEpoch_ = epoch;
        
        // Note: No disk cache - DAG is GPU-resident only (zero-copy, minimal RAM)
        // Regeneration on restart is fast with GPU acceleration
        
        return dagData_;
    }

    bool hasEpoch(uint32_t epoch) const {
        return currentEpoch_ == epoch && dagData_ != nullptr;
    }

    size_t getSize() const {
        return dagSize_;
    }

    void* getGpuPointer() const {
        return d_dag_;
    }

    uint32_t getCurrentEpoch() const {
        return currentEpoch_;
    }

    void freeHostMemory() {
        if (dagData_) {
            // If we had allocated the full DAG on host dagSize_ will be large;
            // otherwise we allocated a small placeholder. Report accordingly.
            bool placeholder = (dagSize_ == 0) || (dagSize_ < (4 * 1024 * 1024));
            std::free(dagData_);
            dagData_ = nullptr;
            if (!placeholder) {
                size_t freedMB = dagSize_ / (1024 * 1024);
                LOG_INFO("✓ Freed " + std::to_string(freedMB) + " MB of DAG from host RAM");
            } else {
                LOG_INFO("✓ Freed host DAG placeholder (minimal RAM reclaimed)");
            }
        }
    }

    void setCacheDir(const std::string& path) {
        cacheDir_ = path;
        std::filesystem::create_directories(cacheDir_);
    }

    bool loadFromCache(uint32_t epoch) {
        if (cacheDir_.empty()) return false;

        std::string filename = cacheDir_ + "/dag-" + std::to_string(epoch) + ".bin";
        
        if (!std::filesystem::exists(filename)) {
            return false;
        }
        
        LOG_DEBUG("Loading DAG from cache: " + filename);
        
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Read DAG size
        size_t fileSize = std::filesystem::file_size(filename);
        if (fileSize < 64) { // Invalid / truncated cache, regenerate
            LOG_WARN("DAG cache file too small (" + std::to_string(fileSize) + " bytes) – regenerating DAG");
            return false;
        }

        // Validate against expected dataset size for epoch
        uint64_t expectedSize = Ethash::getDatasetSize(epoch);
        if (fileSize != expectedSize) {
            LOG_WARN("DAG cache size mismatch: file=" + std::to_string(fileSize) +
                     " bytes, expected=" + std::to_string(expectedSize) +
                     " bytes. Ignoring cache and regenerating DAG.");
            return false;
        }
        
        // Allocate memory (use malloc to match free)
        cleanup();
        dagData_ = std::malloc(fileSize);
        if (!dagData_) {
            LOG_ERROR("Failed to allocate memory for DAG cache load");
            return false;
        }
        dagSize_ = fileSize;
        
        // Read DAG data
        file.read(static_cast<char*>(dagData_), fileSize);
        
        if (!file.good()) {
            LOG_ERROR("Failed to read DAG from cache");
            cleanup();
            return false;
        }
        
        return true;
    }

    bool saveToCache(uint32_t epoch) {
        if (cacheDir_.empty() || dagData_ == nullptr) return false;

        std::string filename = cacheDir_ + "/dag-" + std::to_string(epoch) + ".bin";
        std::ofstream file(filename, std::ios::binary);
        
        if (!file.is_open()) {
            LOG_WARN("Failed to open cache file for writing");
            return false;
        }

        // Write DAG data
        file.write(static_cast<const char*>(dagData_), dagSize_);
        
        if (!file.good()) {
            LOG_ERROR("Failed to write DAG to cache");
            return false;
        }
        
        return true;
    }

private:
    void cleanup() {
        if (dagData_ != nullptr) {
            std::free(dagData_);
            dagData_ = nullptr;
        }
        if (d_dag_ != nullptr) {
            cudaFree(d_dag_);
            d_dag_ = nullptr;
        }
        dagSize_ = 0;
    }

    uint32_t currentEpoch_;
    void* dagData_;        // Host placeholder (128 bytes)
    void* d_dag_;          // GPU DAG pointer
    size_t dagSize_;
    std::string cacheDir_;
};

// DagGenerator implementation
DagGenerator::DagGenerator()
    : pImpl_(std::make_unique<Impl>())
{}

DagGenerator::~DagGenerator() = default;

const void* DagGenerator::generate(uint32_t epoch, bool useGpu) {
    return pImpl_->generate(epoch, useGpu);
}

bool DagGenerator::hasEpoch(uint32_t epoch) const {
    return pImpl_->hasEpoch(epoch);
}

size_t DagGenerator::getSize() const {
    return pImpl_->getSize();
}

void* DagGenerator::getGpuPointer() const {
    return pImpl_->getGpuPointer();
}

uint32_t DagGenerator::getCurrentEpoch() const {
    return pImpl_->getCurrentEpoch();
}

void DagGenerator::setCacheDir(const std::string& path) {
    pImpl_->setCacheDir(path);
}

bool DagGenerator::loadFromCache(uint32_t epoch) {
    return pImpl_->loadFromCache(epoch);
}

bool DagGenerator::saveToCache(uint32_t epoch) {
    return pImpl_->saveToCache(epoch);
}

void DagGenerator::freeHostMemory() {
    pImpl_->freeHostMemory();
}

} // namespace dag
} // namespace ohmy
