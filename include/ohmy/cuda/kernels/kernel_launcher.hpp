#include "ohmy/types.hpp"
#include <stdint.h>
#include <stdint.h>
#pragma once

#include <cstdint>
#include <cuda_runtime.h>
#include "ohmy/cuda/core/device_solution.hpp"

namespace ohmy {
namespace cuda {

// Which kernel variant to use. Auto defers to environment or defaults.
enum class KernelVariant {
    Auto = 0,
    Base,
    Optimized,
    Texture
};

struct KernelLaunchParams {
    // Common inputs
    const uint64_t* d_dag = nullptr;
    uint64_t dagSize = 0;
    const uint32_t* d_header = nullptr;
    const uint32_t* d_seedHash = nullptr;
    const uint8_t*  d_targetBE = nullptr;

    uint64_t startNonce = 0;
    uint64_t searchCount = 0;

    // Outputs / counters
    DeviceSolution* d_solutions = nullptr;
    uint32_t* d_solutionCount = nullptr;
    uint32_t maxSolutions = 0;

    // Execution
    cudaStream_t stream = nullptr;

    // Optional: texture path (disabled by default for >4GB DAG limits)
    cudaTextureObject_t texDAG = 0;

    // Variant selection (Auto uses env OHMY_USE_OPTIMIZED_KERNEL=1)
    KernelVariant variant = KernelVariant::Auto;
};

// Launches the selected kernel variant after applying project-wide policy
// and environment-based tuning (OHMY_USE_OPTIMIZED_KERNEL, OHMY_NONCES_PER_THREAD).
void launch_search_kernel(const KernelLaunchParams& params);

} // namespace cuda
} // namespace ohmy
