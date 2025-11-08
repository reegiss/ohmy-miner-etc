# CUDA Optimization Guide

## GPU Architecture

### Memory Hierarchy (from fastest to slowest)

1. **Registers** (per thread)
   - ~32 registers per thread
   - Fastest access (~1 cycle)
   
2. **Shared Memory** (per block)
   - 48-96 KB per SM
   - Low latency (~20 cycles)
   - Explicitly managed
   
3. **L1/L2 Cache**
   - Automatic caching
   - ~100 cycles
   
4. **Global Memory** (device)
   - 4-24 GB VRAM
   - High latency (~400 cycles)
   - High bandwidth (200-900 GB/s)

## Optimization Techniques

### 1. Coalesced Memory Access

**Bad (uncoalesced):**
```cuda
// Each thread accesses random locations
__global__ void bad_kernel(uint64_t* data) {
    int idx = threadIdx.x * 137;  // Random stride
    uint64_t val = data[idx];
}
```

**Good (coalesced):**
```cuda
// Threads access consecutive memory
__global__ void good_kernel(uint64_t* data) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t val = data[idx];
}
```

### 2. Shared Memory Usage

```cuda
__global__ void ethash_kernel(uint64_t* dag, uint64_t dagSize) {
    // Shared memory for frequently accessed DAG items
    __shared__ uint64_t sharedDag[256];
    
    // Cooperative loading
    int tid = threadIdx.x;
    int dagIdx = blockIdx.x * 256 + tid;
    if (dagIdx < dagSize) {
        sharedDag[tid] = dag[dagIdx];
    }
    __syncthreads();
    
    // Use sharedDag instead of global memory
    uint64_t mix = sharedDag[tid % 256];
}
```

### 3. Occupancy Optimization

Calculate optimal block size:
```bash
# Use CUDA Occupancy Calculator
# Target: 75%+ occupancy

# Formula:
blocks_per_SM = min(
    max_blocks_per_SM,
    max_warps_per_SM / warps_per_block,
    shared_memory_per_SM / shared_memory_per_block
)
```

### 4. Register Pressure

```cuda
// Reduce register usage
__global__ void __launch_bounds__(256, 4)  // maxThreadsPerBlock, minBlocksPerSM
optimized_kernel() {
    // Compiler will optimize register usage
}
```

### 5. Async Operations

```cuda
// Overlap compute and memory transfer
cudaStream_t stream1, stream2;
cudaStreamCreate(&stream1);
cudaStreamCreate(&stream2);

// Queue operations on different streams
cudaMemcpyAsync(d_data1, h_data1, size, H2D, stream1);
kernel<<<grid, block, 0, stream1>>>(d_data1);

cudaMemcpyAsync(d_data2, h_data2, size, H2D, stream2);
kernel<<<grid, block, 0, stream2>>>(d_data2);
```

## Mining-Specific Optimizations

### DAG Access Pattern

```cuda
// Use texture memory for read-only DAG
texture<uint4, cudaTextureType1D, cudaReadModeElementType> dagTexture;

__global__ void search_kernel() {
    uint4 dagItem = tex1Dfetch(dagTexture, dagIndex);
}
```

### Nonce Distribution

```cuda
// Each thread processes multiple nonces
__global__ void batch_search(uint64_t startNonce, uint32_t noncesPerThread) {
    uint64_t baseNonce = startNonce + (blockIdx.x * blockDim.x + threadIdx.x) * noncesPerThread;
    
    for (uint32_t i = 0; i < noncesPerThread; ++i) {
        uint64_t nonce = baseNonce + i;
        // Process nonce
    }
}
```

### Warp-Level Primitives

```cuda
#include <cooperative_groups.h>

__global__ void warp_optimized() {
    namespace cg = cooperative_groups;
    cg::thread_block_tile<32> warp = cg::tiled_partition<32>(cg::this_thread_block());
    
    // Check if any thread found solution
    if (warp.any(foundSolution)) {
        // Handle solution
    }
}
```

## Profiling

### NVIDIA Nsight Compute

```bash
# Profile kernel
ncu --set full -o profile ./ohmy-miner-etc

# Key metrics:
# - SM Efficiency
# - Memory Bandwidth Utilization  
# - Occupancy
# - Register Usage
# - Shared Memory Usage
```

### NVIDIA Nsight Systems

```bash
# Timeline profiling
nsys profile --stats=true ./ohmy-miner-etc

# Look for:
# - Kernel launch overhead
# - Memory transfer time
# - CPU-GPU synchronization
```

## GPU-Specific Tuning

### Pascal (GTX 10xx, P100)
- CUDA Arch: `sm_60`, `sm_61`
- Shared Memory: 48 KB
- Registers: 64K per SM
- Block size: 128-256

### Turing (RTX 20xx)
- CUDA Arch: `sm_75`
- Tensor Cores (not useful for mining)
- Block size: 256

### Ampere (RTX 30xx, A100)
- CUDA Arch: `sm_80`, `sm_86`
- Shared Memory: 96 KB (A100)
- Async memory operations
- Block size: 256-512

### Ada (RTX 40xx)
- CUDA Arch: `sm_89`
- Improved L2 cache
- Higher clock speeds
- Block size: 256-512

## References

- [CUDA C Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [CUDA Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/)
- [Nsight Compute Documentation](https://docs.nvidia.com/nsight-compute/)
