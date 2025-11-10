#ifndef OHMY_CUDA_CHECK_HPP
#define OHMY_CUDA_CHECK_HPP

#include <cuda_runtime.h>
#include "ohmy/logger.hpp"

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
