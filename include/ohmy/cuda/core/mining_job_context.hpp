#include "ohmy/types.hpp"
#include <stdint.h>
#include <cstdint>
#ifndef OHMY_CUDA_MINING_JOB_CONTEXT_HPP
#define OHMY_CUDA_MINING_JOB_CONTEXT_HPP

#include <stdint.h>

#include <string>
#include <array>
#include <atomic>
#include <mutex>
#include "ohmy/types.hpp"

namespace ohmy { namespace cuda {

struct MiningJobContext {
    std::string jobId;              // Stratum job ID
    hash32_t headerHash;            // 32-byte header (as hash32_t alias)
    hash32_t seedHash;              // 32-byte seed hash from pool
    std::array<uint8_t, 32> targetBE; // Big-endian target threshold
    uint32_t epoch;                 // Current DAG epoch
    std::atomic<bool> isValid{false};
    std::atomic<uint64_t> timestamp{0};
    std::mutex jobMutex;            // Protect updates to context
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_MINING_JOB_CONTEXT_HPP
