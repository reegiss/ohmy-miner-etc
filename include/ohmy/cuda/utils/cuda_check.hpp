#ifndef OHMY_CUDA_CHECK_HPP
#define OHMY_CUDA_CHECK_HPP

#include <cuda_runtime.h>
#include "ohmy/logger.hpp"

/**
 * CUDA_CHECK(call)
 * ----------------
 * Unified error handling macro wrapping CUDA runtime API calls. Throws a
 * std::runtime_error after logging when an error is encountered.
 *
 * Rationale:
 *  - Centralize logging format
 *  - Allow early exit from failing CUDA operations while preserving call site
 *  - Encourage consistent exception-based error propagation upstream
 */
#ifndef CUDA_CHECK
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            LOG_ERROR(std::string("CUDA error: ") + cudaGetErrorString(err)); \
            throw std::runtime_error("CUDA error"); \
        } \
    } while (0)
#endif

#endif // OHMY_CUDA_CHECK_HPP
