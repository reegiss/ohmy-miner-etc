# Ethash Algorithm Implementation Guide

## Overview

Ethash is the Proof-of-Work (PoW) algorithm used by Ethereum Classic. It's designed to be:
- Memory-hard (requires significant VRAM)
- ASIC-resistant
- Light-client verifiable

## Core Components

### 1. DAG (Directed Acyclic Graph)

The DAG is a large dataset (~4GB) that grows over time. Each epoch (30,000 blocks) requires a new DAG.

**Generation Process:**
1. Calculate epoch from block number: `epoch = block_number / 30000`
2. Generate seed hash for epoch
3. Create cache (~16MB) using seed
4. Generate full DAG from cache

### 2. Cache

The cache is a smaller dataset used to:
- Generate the full DAG
- Verify mining solutions without storing full DAG

**Size:** `16MB + (128KB * epoch)`

### 3. Mining Algorithm

```
For each nonce:
  1. Mix header + nonce → seed (64 bytes)
  2. Read 64 DAG items using seed
  3. Apply FNV mixing 
  4. Calculate final hash
  5. Check if hash < target
```

## Implementation Details

### CUDA Optimization

**Memory Access Pattern:**
```cuda
// Coalesced memory access
__shared__ uint64_t sharedDag[256];

// Each thread loads consecutive memory
int tid = threadIdx.x;
sharedDag[tid] = dag[blockIdx.x * blockDim.x + tid];
__syncthreads();
```

**Kernel Configuration:**
- Blocks: 2048-8192 (depending on GPU)
- Threads per block: 128-256
- Registers per thread: ~32
- Shared memory: 8-16 KB per block

### Performance Tips

1. **DAG Caching:** Store generated DAG on disk to avoid regeneration
2. **Memory Bandwidth:** Use texture memory for read-only DAG access
3. **Warp Efficiency:** Ensure all threads in warp follow same path
4. **Occupancy:** Aim for 75%+ occupancy on modern GPUs

## References

- [Ethereum Yellow Paper](https://ethereum.github.io/yellowpaper/paper.pdf)
- [Ethash Specification](https://github.com/ethereum/wiki/wiki/Ethash)
- [ethminer Implementation](https://github.com/ethereum-mining/ethminer)
