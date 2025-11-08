# Quick Reference Guide

## Build Commands

```bash
# Standard build
./scripts/build.sh

# Debug build with GPU debugging
BUILD_TYPE=Debug ./scripts/build.sh

# Build for specific GPU architectures
CUDA_ARCH="75;86" ./scripts/build.sh

# Manual build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Run Commands

```bash
# Using script
WALLET=0xYourAddress ./scripts/start-mining.sh

# Direct execution
./build/ohmy-miner-etc \
  --pool stratum+tcp://etc.2miners.com:1010 \
  --wallet 0xYourWalletAddress \
  --worker miner01

# From config file
./build/ohmy-miner-etc --config config.json

# Benchmark mode
./build/ohmy-miner-etc --benchmark
```

## Test Commands

```bash
# All tests
cd build && ctest --verbose

# Unit tests only
./build/tests/unit_tests

# Benchmarks
./build/tests/benchmark_tests
```

## Common Development Tasks

### Adding a New Feature

1. **Define interface** in `include/ohmy/feature.hpp`
2. **Implement** in `src/component/feature.cpp`
3. **Add tests** in `tests/unit/test_feature.cpp`
4. **Update CMakeLists.txt** to include new files
5. **Build and test**

### Optimizing CUDA Kernel

1. **Profile current performance:**
   ```bash
   ncu --set full -o profile ./build/ohmy-miner-etc
   ```

2. **Check metrics:**
   - SM Efficiency
   - Memory Bandwidth
   - Occupancy
   - Warp Execution Efficiency

3. **Modify kernel** in `src/cuda/kernels/`

4. **Benchmark:**
   ```bash
   ./build/tests/benchmark_tests
   ```

### Debugging CUDA Code

```bash
# Build with debug symbols
BUILD_TYPE=Debug ./scripts/build.sh

# Run with cuda-memcheck
cuda-memcheck ./build/ohmy-miner-etc

# Run with compute-sanitizer
compute-sanitizer ./build/ohmy-miner-etc
```

## Code Patterns

### Creating a New Class

```cpp
// include/ohmy/myclass.hpp
#pragma once
#include <memory>

namespace ohmy {

class MyClass {
public:
    MyClass();
    ~MyClass();
    
    // Disable copy
    MyClass(const MyClass&) = delete;
    MyClass& operator=(const MyClass&) = delete;
    
    // Methods
    void doSomething();
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace ohmy
```

```cpp
// src/component/myclass.cpp
#include "ohmy/myclass.hpp"

namespace ohmy {

class MyClass::Impl {
public:
    void doSomething() {
        // Implementation
    }
};

MyClass::MyClass() 
    : pImpl_(std::make_unique<Impl>()) 
{}

MyClass::~MyClass() = default;

void MyClass::doSomething() {
    pImpl_->doSomething();
}

} // namespace ohmy
```

### Adding a CUDA Kernel

```cpp
// src/cuda/kernels/my_kernel.cu
#include <cuda_runtime.h>

namespace ohmy {
namespace cuda {

__global__ void my_kernel(
    const uint64_t* input,
    uint64_t* output,
    size_t size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) {
        output[idx] = input[idx] * 2;
    }
}

// Launch wrapper
void launchMyKernel(
    const uint64_t* d_input,
    uint64_t* d_output,
    size_t size
) {
    int blockSize = 256;
    int gridSize = (size + blockSize - 1) / blockSize;
    
    my_kernel<<<gridSize, blockSize>>>(
        d_input, d_output, size
    );
    
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        throw std::runtime_error(cudaGetErrorString(err));
    }
}

} // namespace cuda
} // namespace ohmy
```

## Troubleshooting

### Build Errors

**CUDA not found:**
```bash
# Install CUDA Toolkit
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
sudo dpkg -i cuda-keyring_1.0-1_all.deb
sudo apt-get update
sudo apt-get install cuda-toolkit-12-3
```

**Missing dependencies:**
```bash
# Install required packages
sudo apt-get install build-essential cmake libssl-dev
```

### Runtime Errors

**No CUDA devices found:**
```bash
# Check driver
nvidia-smi

# Update driver if needed
sudo apt-get install nvidia-driver-535
```

**Out of memory:**
- Reduce intensity: `--intensity 20`
- Use fewer GPUs: `--devices 0,1`
- Clear DAG cache: `rm -rf ./dag-cache/*`

**Pool connection failed:**
- Check pool URL
- Verify wallet address
- Try different pool
- Check firewall settings

## Performance Tuning

### Finding Optimal Settings

1. **Start conservative:**
   ```bash
   --intensity 18 --threads 1
   ```

2. **Increase intensity:**
   ```bash
   --intensity 19  # Test
   --intensity 20  # Test
   --intensity 21  # Default
   ```

3. **Add threads:**
   ```bash
   --threads 2  # If GPU not maxed
   ```

4. **Monitor:**
   ```bash
   watch -n 1 nvidia-smi
   # Look for: GPU utilization, memory usage, temperature
   ```

### GPU-Specific Recommendations

| GPU Model  | Intensity | Threads | Expected MH/s |
|------------|-----------|---------|---------------|
| GTX 1660   | 19        | 1       | 25-30         |
| RTX 2060   | 20        | 1       | 35-40         |
| RTX 3060   | 20        | 1       | 45-50         |
| RTX 3070   | 21        | 1       | 60-65         |
| RTX 3080   | 21        | 2       | 90-100        |
| RTX 3090   | 22        | 2       | 120-130       |
| RTX 4070   | 21        | 1       | 70-80         |
| RTX 4080   | 22        | 2       | 130-140       |
| RTX 4090   | 22        | 2       | 150-170       |

## Useful Links

- **Documentation:** `docs/ARCHITECTURE.md`
- **Ethash Guide:** `docs/ETHASH.md`
- **Stratum Guide:** `docs/STRATUM.md`
- **CUDA Guide:** `docs/CUDA_OPTIMIZATION.md`

## Environment Variables

```bash
# CUDA
export CUDA_VISIBLE_DEVICES=0,1,2  # Select GPUs
export CUDA_LAUNCH_BLOCKING=1       # Debug sync

# Mining
export POOL_URL="stratum+tcp://pool.example.com:1010"
export WALLET="0xYourAddress"
export WORKER="miner01"

# Logging
export LOG_LEVEL=DEBUG
export LOG_FILE=/var/log/miner.log
```

## Configuration File Format

```json
{
  "pool": {
    "url": "stratum+tcp://etc.2miners.com:1010",
    "wallet": "0xYourWalletAddress",
    "worker": "worker01",
    "use_tls": false
  },
  "devices": {
    "gpu_ids": [0, 1],
    "threads_per_gpu": 1,
    "intensity": 21
  },
  "general": {
    "log_file": "ohmy-miner.log",
    "log_level": "INFO",
    "dag_cache_dir": "./dag-cache",
    "benchmark": false
  }
}
```
