#include "ohmy/cuda/kernels/kernel_launcher.hpp"
#include "ohmy/cuda/kernels/launchers_fwd.hpp"
#include "ohmy/logger.hpp"
#include <cstdlib>
#include <atomic>
#include <string>

namespace ohmy {
namespace cuda {

struct KernelEnvConfig {
    bool initialized = false;
    bool useOptimized = false;
    bool useTexture = false; // future: texture path
    uint32_t noncesPerThread = 1;
};

static KernelEnvConfig& envCfg() {
    static KernelEnvConfig cfg; // thread-safe init (C++11)
    if (!cfg.initialized) {
        const char* opt = std::getenv("OHMY_USE_OPTIMIZED_KERNEL");
        cfg.useOptimized = (opt && std::string(opt) == "1");

        const char* tex = std::getenv("OHMY_USE_TEXTURE_KERNEL");
        cfg.useTexture = (tex && std::string(tex) == "1");

        const char* nptr = std::getenv("OHMY_NONCES_PER_THREAD");
        if (nptr) {
            int v = std::atoi(nptr);
            if (v > 0 && v <= 256) cfg.noncesPerThread = static_cast<uint32_t>(v);
        }

        // One-time informational log
        LOG_INFO("KernelEnvConfig: optimized=" + std::string(cfg.useOptimized ? "true" : "false") +
                 " texture=" + std::string(cfg.useTexture ? "true" : "false") +
                 " noncesPerThread=" + std::to_string(cfg.noncesPerThread));
        cfg.initialized = true;
    }
    return cfg;
}

void launch_search_kernel(const KernelLaunchParams& params) {
    if (!params.d_dag || !params.d_header || !params.d_seedHash || !params.d_targetBE ||
        !params.d_solutions || !params.d_solutionCount || params.maxSolutions == 0) {
        LOG_ERROR("launch_search_kernel: invalid parameters (null pointers or zero maxSolutions)");
        return; // fail fast; do not throw inside hot path
    }

    const KernelEnvConfig& cfg = envCfg();

    // Resolve variant
    KernelVariant resolved = params.variant;
    if (resolved == KernelVariant::Auto) {
        if (cfg.useTexture) {
            resolved = KernelVariant::Texture; // if user explicitly asked
        } else if (cfg.useOptimized) {
            resolved = KernelVariant::Optimized;
        } else {
            resolved = KernelVariant::Base;
        }
    }

    switch (resolved) {
        case KernelVariant::Texture: {
            if (params.texDAG == 0) {
                LOG_WARN("Texture kernel requested but texDAG==0. Falling back to optimized/base.");
                // Fallback chain: optimized if enabled else base
                if (cfg.useOptimized) {
                    launch_ethash_search_optimized(
                        params.d_dag,
                        params.dagSize,
                        params.d_header,
                        params.d_seedHash,
                        params.d_targetBE,
                        params.startNonce,
                        params.searchCount,
                        cfg.noncesPerThread,
                        params.d_solutions,
                        params.d_solutionCount,
                        params.maxSolutions,
                        params.stream
                    );
                } else {
                    launch_ethash_search(
                        params.d_dag,
                        params.dagSize,
                        params.d_header,
                        params.d_seedHash,
                        params.d_targetBE,
                        params.startNonce,
                        params.searchCount,
                        params.d_solutions,
                        params.d_solutionCount,
                        params.maxSolutions,
                        params.stream
                    );
                }
            } else {
                launch_ethash_search_texture(
                    params.texDAG,
                    params.dagSize,
                    params.d_header,
                    params.d_seedHash,
                    params.d_targetBE,
                    params.startNonce,
                    params.searchCount,
                    cfg.noncesPerThread,
                    params.d_solutions,
                    params.d_solutionCount,
                    params.maxSolutions,
                    params.stream
                );
            }
            break;
        }
        case KernelVariant::Optimized: {
            launch_ethash_search_optimized(
                params.d_dag,
                params.dagSize,
                params.d_header,
                params.d_seedHash,
                params.d_targetBE,
                params.startNonce,
                params.searchCount,
                envCfg().noncesPerThread,
                params.d_solutions,
                params.d_solutionCount,
                params.maxSolutions,
                params.stream
            );
            break;
        }
        case KernelVariant::Base: {
            launch_ethash_search(
                params.d_dag,
                params.dagSize,
                params.d_header,
                params.d_seedHash,
                params.d_targetBE,
                params.startNonce,
                params.searchCount,
                params.d_solutions,
                params.d_solutionCount,
                params.maxSolutions,
                params.stream
            );
            break;
        }
        case KernelVariant::Auto: // already resolved
        default:
            launch_ethash_search(
                params.d_dag,
                params.dagSize,
                params.d_header,
                params.d_seedHash,
                params.d_targetBE,
                params.startNonce,
                params.searchCount,
                params.d_solutions,
                params.d_solutionCount,
                params.maxSolutions,
                params.stream
            );
            break;
    }
}

} // namespace cuda
} // namespace ohmy
