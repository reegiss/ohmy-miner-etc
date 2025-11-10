# Kernel Launcher Abstraction

Centralizes kernel variant selection and launch configuration for Ethash search kernels.

## Overview

The launcher wraps the existing host launch functions:
- `launch_ethash_search` (base)
- `launch_ethash_search_optimized` (batched, tuning)
- `launch_ethash_search_texture` (optional; disabled by default due to >4GB DAG limits)

It provides a single interface to choose the variant and apply environment-based tuning.

## Public API

Header: `include/ohmy/cuda/kernels/kernel_launcher.hpp`

- `enum class KernelVariant { Auto, Base, Optimized, Texture }`
- `struct KernelLaunchParams { ... }`
- `void launch_search_kernel(const KernelLaunchParams& params)`

## Environment Variables

- `OHMY_USE_OPTIMIZED_KERNEL=1` — prefer optimized variant (default: base)
- `OHMY_NONCES_PER_THREAD=<N>` — batching per thread for optimized/texture variants (default: 1)
- `OHMY_USE_TEXTURE_KERNEL=1` — force texture variant (requires `texDAG`) — off by default

One-time boot log prints the resolved configuration for visibility.

## Rationale

- Removes duplicated kernel selection logic scattered across `device_manager.cu`
- Single point to tune batching and variant policy
- Keeps base/optimized/texture host launchers intact and reusable

## Notes

- Texture path is kept optional and guarded; for production DAG sizes, linear memory path is the default.
- Grid/block sizing remains inside the underlying launcher functions for now; can be centralized later if desired.
