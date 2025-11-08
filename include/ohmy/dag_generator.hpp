#pragma once

#include "types.hpp"
#include <memory>
#include <string>

namespace ohmy {
namespace dag {

/**
 * @brief DAG (Directed Acyclic Graph) generator for Ethash
 */
class DagGenerator {
public:
    DagGenerator();
    ~DagGenerator();

    // Disable copy
    DagGenerator(const DagGenerator&) = delete;
    DagGenerator& operator=(const DagGenerator&) = delete;

    /**
     * @brief Generate DAG for given epoch
     * @param epoch Epoch number
     * @param useGpu Generate on GPU if true, CPU otherwise
     * @return Pointer to DAG data
     */
    const void* generate(uint32_t epoch, bool useGpu = true);

    /**
     * @brief Check if DAG is already generated for epoch
     */
    bool hasEpoch(uint32_t epoch) const;

    /**
     * @brief Get DAG size for current epoch
     */
    size_t getSize() const;

    /**
     * @brief Get current epoch
     */
    uint32_t getCurrentEpoch() const;

    /**
     * @brief Set cache directory for DAG files
     */
    void setCacheDir(const std::string& path);

    /**
     * @brief Load DAG from cache file
     */
    bool loadFromCache(uint32_t epoch);

    /**
     * @brief Save DAG to cache file
     */
    bool saveToCache(uint32_t epoch);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace dag
} // namespace ohmy
