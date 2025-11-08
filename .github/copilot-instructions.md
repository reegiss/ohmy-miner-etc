# Copilot Instructions

This file provides guidance for AI coding agents working on ohmy-miner-etc codebase.

## Project Overview

**ohmy-miner-etc** is a high-performance Ethereum Classic (ETC) miner built from scratch using C++ and CUDA. The project leverages GPU acceleration for Ethash algorithm mining, following modern C++ best practices and CUDA optimization techniques.

## Architecture

### Core Components
- **Mining Engine**: CUDA kernels for Ethash algorithm implementation
- **Network Layer**: Stratum protocol client for pool communication
- **Device Manager**: GPU detection, initialization, and resource management
- **DAG Generator**: Epoch-based Directed Acyclic Graph creation and caching
- **Hash Rate Monitor**: Performance tracking and statistics

### Data Flow
1. Pool connection establishes mining job parameters
2. DAG generation for current epoch (cached for reuse)
3. CUDA kernels search for valid nonces in parallel
4. Valid solutions submitted to pool
5. Continuous performance monitoring and adaptive tuning

## Developer Workflows

### Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Test
```bash
cd build
ctest --verbose
# or run specific test suites
./tests/unit_tests
./tests/benchmark_tests
```

### Run
```bash
# Development mode with verbose logging
./ohmy-miner-etc --pool <pool_url> --wallet <wallet_address> --verbose

# Production mode
./ohmy-miner-etc --pool <pool_url> --wallet <wallet_address> --threads <num_gpus>
```

## Code Conventions

### C++ Standards
- Use **C++17** or later features
- RAII for all resource management (memory, CUDA contexts, network connections)
- Smart pointers (unique_ptr, shared_ptr) over raw pointers
- Const correctness throughout the codebase
- Namespace organization: `ohmy::miner`, `ohmy::cuda`, `ohmy::network`, `ohmy::utils`

### CUDA Best Practices
- Separate `.cu` files for CUDA kernels
- Use `__device__`, `__host__`, `__global__` qualifiers appropriately
- Optimize memory access patterns (coalesced global memory access)
- Shared memory utilization for frequently accessed data
- Stream-based async operations for overlapping compute and memory transfers
- Error checking after every CUDA API call
- Kernel launch configurations tuned per GPU architecture

### GPU-First Development Philosophy
- **ALWAYS prefer GPU over CPU for compute-intensive operations**
- DAG generation MUST use GPU kernels (not CPU multi-threading)
- Hash calculations MUST run on GPU (Keccak, Ethash, FNV)
- Only use CPU for: control flow, network I/O, configuration, logging
- When implementing new features, ask: "Can this run on GPU?" before writing CPU code
- CPU fallback is acceptable ONLY for error recovery or unsupported hardware
- Multi-threading on CPU is a last resort, not a primary optimization strategy

### Error Handling
- Exceptions for recoverable errors
- Logging levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- CUDA error wrapper macros for consistent error checking
- Graceful degradation when GPUs fail

### Memory Management
- Pool allocators for frequent small allocations
- CUDA unified memory where beneficial
- Pinned (page-locked) memory for host-device transfers
- Proper cleanup in destructors and error paths

## Key Files and Directories

```
src/
├── core/           # Mining engine and Ethash implementation
├── cuda/           # CUDA kernels and GPU management
├── network/        # Stratum protocol and pool communication
├── dag/            # DAG generation and caching
└── utils/          # Logging, configuration, helpers

include/            # Public headers
tests/              # Unit tests and benchmarks
cmake/              # CMake modules and find scripts
docs/               # Technical documentation
```

## Integration Points

### External Dependencies
- **CUDA Toolkit 11+**: GPU compute platform
- **OpenSSL**: Secure pool connections (TLS/SSL)
- **libcurl** or custom socket: HTTP/Stratum network layer
- **nlohmann/json**: JSON parsing for pool communication
- **spdlog**: Fast logging library

### Pool Protocol
- Stratum v1/v2 protocol implementation
- JSON-RPC 2.0 for pool communication
- Subscribe, authorize, submit methods
- Difficulty adjustment handling

### Hardware Requirements
- NVIDIA GPU with CUDA compute capability 6.0+ (Pascal or newer)
- Minimum 4GB VRAM for DAG storage
- Multi-GPU support with independent mining threads

## Common Tasks

### Adding a New CUDA Kernel
1. Create kernel in `src/cuda/kernels/`
2. Declare in corresponding header
3. Add error checking wrappers
4. Benchmark and optimize occupancy
5. Document memory requirements and launch parameters

### Implementing Pool Feature
1. Update `src/network/stratum_client.cpp`
2. Handle JSON message parsing
3. Update state machine for new protocol states
4. Add integration tests

### Performance Tuning
- Profile with NVIDIA Nsight Compute
- Adjust block/grid dimensions for target GPU
- Monitor kernel occupancy and memory bandwidth
- Test across different GPU architectures (Pascal, Turing, Ampere, Ada)
