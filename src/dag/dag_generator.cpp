#include "ohmy/dag_generator.hpp"
#include "ohmy/ethash.hpp"
#include "ohmy/logger.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>

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
        
        LOG_INFO("  Cache size: " + std::to_string(cacheSize / 1024 / 1024) + " MB");
        LOG_INFO("  DAG size: " + std::to_string(dagSize_ / 1024 / 1024) + " MB");
        LOG_INFO("  Items: " + std::to_string(numItems));

        // Generate cache
        LOG_INFO("  Generating cache...");
        auto cache = Ethash::calculateCache(epoch);
        LOG_INFO("  ✓ Cache generated (" + std::to_string(cache.size()) + " items)");

        // Allocate DAG memory
        cleanup(); // Free old DAG if exists
        dagData_ = operator new(dagSize_);
        hash64_t* dag = static_cast<hash64_t*>(dagData_);

        // Generate DAG items
        LOG_INFO("  Generating DAG items...");
        auto startTime = std::chrono::steady_clock::now();
        
        if (useGpu) {
            // For now, fall back to CPU (GPU generation can be added later)
            LOG_WARN("  GPU generation not implemented yet, using CPU");
        }
        
        // CPU generation with progress reporting
        const uint32_t reportInterval = numItems / 10; // Report every 10%
        for (uint32_t i = 0; i < numItems; i++) {
            dag[i] = Ethash::calculateDatasetItem(cache, i);
            
            if (reportInterval > 0 && (i + 1) % reportInterval == 0) {
                uint32_t percent = ((i + 1) * 100) / numItems;
                LOG_INFO("    Progress: " + std::to_string(percent) + "%");
            }
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
        
        // Allocate memory
        cleanup();
        dagData_ = operator new(fileSize);
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
            operator delete(dagData_);
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
