#ifndef OHMY_CUDA_DEVICE_SOLUTION_HPP
#define OHMY_CUDA_DEVICE_SOLUTION_HPP

#include <cstdint>

namespace ohmy { namespace cuda {

// POD layout shared between host and device for found solutions
struct DeviceSolution {
    uint64_t nonce;
    uint8_t mixHash[32];
    uint8_t result[32];
};

}} // namespace ohmy::cuda

#endif // OHMY_CUDA_DEVICE_SOLUTION_HPP
