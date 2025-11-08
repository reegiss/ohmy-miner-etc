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
#include <cuda_runtime.h>

namespace ohmy {
namespace dag {

class DagGenerator::Impl {
public:
    Impl() : currentEpoch_(0), dagData_(nullptr), dagSize_(0) {}

    ~Impl() {
        cleanup();
    }

    const void* generate(uint32_t epoch, bool useGpu) {
        if (currentEpoch_ == epoch && dagData_ != nullptr) {
            return dagData_;
        }

        LOG_INFO("Generating DAG for epoch " + std::to_string(epoch) + "...");

        // Try to load from cache first
        if (loadFromCache(epoch)) {
            LOG_INFO("✓ DAG loaded from cache");
            currentEpoch_ = epoch;
            return dagData_;
        }

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

        // Allocate DAG memory
        // Free old DAG if exists (but preserve dagSize_ for new allocation)
        if (dagData_ != nullptr) {
            std::free(dagData_);
            dagData_ = nullptr;
        }
        
        size_t allocSize = dagSize_;
        LOG_INFO("  Allocating " + std::to_string(allocSize / (1024*1024)) + " MB for DAG (" + std::to_string(allocSize) + " bytes)...");
        try {
            dagData_ = std::malloc(allocSize);
            if (dagData_ == nullptr) {
                LOG_ERROR("Failed to allocate DAG memory");
                return nullptr;
            }
            LOG_INFO("  ✓ Memory allocated at " + std::to_string(reinterpret_cast<uintptr_t>(dagData_)));
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during allocation: " + std::string(e.what()));
            return nullptr;
        }
        
        hash64_t* dag = static_cast<hash64_t*>(dagData_);

        // Generate DAG items
        LOG_INFO("  Generating DAG items...");
        auto startTime = std::chrono::steady_clock::now();
        
        if (useGpu) {
            LOG_INFO("  Using GPU for DAG generation");
            
            // Allocate GPU memory for cache
            void* d_cache = nullptr;
            void* d_dag = nullptr;
            
            size_t cacheBytes = cache.size() * sizeof(hash64_t);
            cudaError_t err = cudaMalloc(&d_cache, cacheBytes);
            if (err != cudaSuccess) {
                LOG_ERROR("  Failed to allocate GPU cache memory: " + std::string(cudaGetErrorString(err)));
                LOG_WARN("  Falling back to CPU generation");
                useGpu = false;
            }
            
            if (useGpu) {
                // Allocate GPU memory for DAG
                err = cudaMalloc(&d_dag, dagSize_);
                if (err != cudaSuccess) {
                    LOG_ERROR("  Failed to allocate GPU DAG memory: " + std::string(cudaGetErrorString(err)));
                    cudaFree(d_cache);
                    LOG_WARN("  Falling back to CPU generation");
                    useGpu = false;
                }
            }
            
            if (useGpu) {
                // Copy cache to GPU
                err = cudaMemcpy(d_cache, cache.data(), cacheBytes, cudaMemcpyHostToDevice);
                if (err != cudaSuccess) {
                    LOG_ERROR("  Failed to copy cache to GPU: " + std::string(cudaGetErrorString(err)));
                    cudaFree(d_cache);
                    cudaFree(d_dag);
                    LOG_WARN("  Falling back to CPU generation");
                    useGpu = false;
                } else {
                    LOG_INFO("  ✓ Cache copied to GPU (" + std::to_string(cacheBytes / (1024*1024)) + " MB)");
                }
            }
            
            if (useGpu) {
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
                    
                    ohmy::cuda::generateDagGpu(d_cache, d_dag, cache.size(), numItems);
                    completed.store(true);
                    monitor.join();
                    
                    LOG_INFO("  ✓ DAG generated on GPU");
                    
                    // Copy DAG back to CPU
                    LOG_INFO("  Copying DAG from GPU to CPU...");
                    err = cudaMemcpy(dagData_, d_dag, dagSize_, cudaMemcpyDeviceToHost);
                    if (err != cudaSuccess) {
                        LOG_ERROR("  Failed to copy DAG from GPU: " + std::string(cudaGetErrorString(err)));
                        cudaFree(d_cache);
                        cudaFree(d_dag);
                        LOG_WARN("  Falling back to CPU generation");
                        useGpu = false;
                    } else {
                        LOG_INFO("  ✓ DAG copied to CPU");
                    }
                    
                    // Cleanup GPU memory
                    cudaFree(d_cache);
                    cudaFree(d_dag);
                } catch (const std::exception& e) {
                    LOG_ERROR("  Exception during GPU generation: " + std::string(e.what()));
                    cudaFree(d_cache);
                    cudaFree(d_dag);
                    LOG_WARN("  Falling back to CPU generation");
                    useGpu = false;
                }
            }
        }
        
        // CPU generation with multi-threading (fallback or if GPU disabled)
        if (!useGpu) {
            const uint32_t numThreads = std::thread::hardware_concurrency();
            LOG_INFO("  Using CPU with " + std::to_string(numThreads) + " thread(s)");
        
            std::vector<std::thread> threads;
            const uint32_t itemsPerThread = numItems / numThreads;
            const uint32_t reportInterval = numItems / 20; // Report every 5%
            
            std::atomic<uint32_t> progress{0};
            std::atomic<uint32_t> lastReported{0};
            
            // Capture cache by reference (read-only, thread-safe)
            const auto& cacheRef = cache;
            
            for (uint32_t t = 0; t < numThreads; t++) {
                uint32_t start = t * itemsPerThread;
                uint32_t end = (t == numThreads - 1) ? numItems : (t + 1) * itemsPerThread;
                
                threads.emplace_back([&cacheRef, dag, start, end, &progress, &lastReported, reportInterval, numItems]() {
                    try {
                        for (uint32_t i = start; i < end; i++) {
                            dag[i] = Ethash::calculateDatasetItem(cacheRef, i);
                            
                            // Update progress
                            uint32_t current = progress.fetch_add(1, std::memory_order_relaxed) + 1;
                            
                            // Report progress periodically
                            if (reportInterval > 0 && current % reportInterval == 0) {
                                uint32_t percent = (current * 100) / numItems;
                                LOG_INFO("    Progress: " + std::to_string(percent) + "%");
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception in DAG generation thread: " + std::string(e.what()));
                    } catch (...) {
                        LOG_ERROR("Unknown exception in DAG generation thread");
                    }
                });
            }
            
            // Wait for all threads to complete
            LOG_INFO("  Waiting for threads to complete...");
            for (auto& thread : threads) {
                thread.join();
            }
            LOG_INFO("  All threads completed");
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
        
        LOG_INFO("  ✓ DAG generated in " + std::to_string(duration.count()) + "s");

        currentEpoch_ = epoch;
        
        // Save to cache
        if (!cacheDir_.empty()) {
            LOG_INFO("  Saving DAG to cache...");
            if (saveToCache(epoch)) {
                LOG_INFO("  ✓ DAG saved to cache");
            } else {
                LOG_WARN("  Failed to save DAG to cache");
            }
        }
        
        return dagData_;
    }

    bool hasEpoch(uint32_t epoch) const {
        return currentEpoch_ == epoch && dagData_ != nullptr;
    }

    size_t getSize() const {
        return dagSize_;
    }

    uint32_t getCurrentEpoch() const {
        return currentEpoch_;
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
        dagSize_ = 0;
    }

    uint32_t currentEpoch_;
    void* dagData_;
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

} // namespace dag
} // namespace ohmy
