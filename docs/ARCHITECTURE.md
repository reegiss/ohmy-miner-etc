# Architecture Overview

For a categorized list of all documents, see [INDEX.md](./INDEX.md). This file focuses on the high-level architecture and how modules fit together.

## Project Structure

```
ohmy-miner-etc/
├── .github/
│   └── copilot-instructions.md    # AI coding agent instructions
├── cmake/
│   └── FindCUDA.cmake              # CMake CUDA finder module
├── docs/
│   ├── ETHASH.md                   # Ethash algorithm guide
│   ├── STRATUM.md                  # Stratum protocol guide
│   └── CUDA_OPTIMIZATION.md        # CUDA optimization techniques
├── include/ohmy/
│   ├── types.hpp                   # Common types and structures
│   ├── miner.hpp                   # Main miner interface
│   ├── ethash.hpp                  # Ethash algorithm
│   ├── cuda/
│   │   ├── core/                   # Device state, solutions, job context (public headers)
│   │   ├── pipeline/               # PipelineManager public headers
│   │   ├── stats/                  # DeviceStatsUtil public headers
│   │   └── utils/                  # CUDA_CHECK and helpers
│   ├── stratum_client.hpp          # Pool communication
│   ├── dag_generator.hpp           # DAG generation
│   ├── logger.hpp                  # Logging system
│   └── config.hpp                  # Configuration management
├── src/
│   ├── main.cpp                    # Application entry point
│   ├── core/
│   │   ├── miner.cpp              # Miner implementation
│   │   └── ethash.cpp             # Ethash implementation
│   ├── cuda/
│   │   ├── core/                   # Mining engine and core logic
│   │   ├── pipeline/               # Async N-stream pipeline implementation
│   │   ├── stats/                  # Stats aggregation implementation
│   │   ├── threading/              # Mining thread orchestration
│   │   └── kernels/
│   │       ├── ethash_kernel.cu    # Legacy/placeholder kernel
│   │       └── search_kernel.cu    # Optimized search kernels (base/optimized/texture/warp)
│   ├── network/
│   │   ├── stratum_client.cpp     # Stratum client
│   │   └── connection.cpp         # Network connection
│   ├── dag/
│   │   └── dag_generator.cpp      # DAG generation
│   └── utils/
│       ├── logger.cpp             # Logger implementation
│       └── config.cpp             # Config parser
├── tests/
│   ├── unit/
│   │   ├── test_ethash.cpp
│   │   ├── test_dag.cpp
│   │   └── test_stratum.cpp
│   └── benchmark/
│       ├── bench_ethash.cpp
│       └── bench_cuda_kernels.cpp
├── scripts/
│   ├── build.sh                    # Build automation
│   └── start-mining.sh             # Mining startup
├── CMakeLists.txt                  # Root CMake configuration
├── config.example.json             # Example configuration
├── README.md                       # Project documentation
├── LICENSE                         # MIT License
└── .gitignore                      # Git ignore rules
```

## Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         Main Application                      │
│                          (main.cpp)                          │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
            ┌────────────────────────┐
            │   Miner (miner.cpp)    │
            │  ┌──────────────────┐  │
            │  │  Orchestrates:   │  │
            │  │  • Mining loop   │  │
            │  │  • Statistics    │  │
            │  │  • Callbacks     │  │
            │  └──────────────────┘  │
            └──┬────────┬─────────┬──┘
               │        │         │
       ┌───────▼──┐  ┌──▼─────┐  └───────────┐
       │ Stratum  │  │  DAG   │              │
       │  Client  │  │ Gen.   │              │
       └────┬─────┘  └───┬────┘              │
            │            │                    │
            │            ▼                    ▼
            │   ┌─────────────────┐  ┌──────────────┐
            │   │  Ethash Algo    │  │   Device     │
            │   │  (ethash.cpp)   │  │   Manager    │
            │   └─────────────────┘  │ (CUDA/.cu)   │
            │                        └──────┬───────┘
            ▼                               │
    ┌──────────────┐                       ▼
    │   Network    │              ┌─────────────────┐
    │  (TCP/TLS)   │              │  CUDA Kernels   │
    └──────────────┘              │  • ethash_hash  │
                                  │  • search       │
                                  │  • dag_gen      │
                                  └─────────────────┘
```

## Data Flow

### 1. Initialization
```
1. Load configuration (config.json or CLI args)
2. Initialize logger
3. Detect CUDA devices
4. Connect to pool (Stratum)
5. Subscribe and authorize
6. Generate DAG for current epoch
```

### 2. Mining Loop
```
┌─────────────────────────────────────────┐
│ Pool sends mining.notify                │
│ • job_id, seed_hash, header_hash        │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│ Miner receives new job                  │
│ • Check if DAG epoch changed            │
│ • Regenerate DAG if needed              │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│ DeviceManager.search()                  │
│ • Launch CUDA kernels                   │
│ • Process nonce ranges in parallel      │
│ • Check results against target          │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│ Solution found?                         │
├────────────┬────────────────────────────┤
│    Yes     │         No                 │
└─────┬──────┴──────────┬─────────────────┘
      │                 │
      ▼                 └─────────────┐
┌─────────────────┐                  │
│ Submit solution │                  │
│ via mining.     │                  │
│ submit          │                  │
└─────┬───────────┘                  │
      │                              │
      ▼                              ▼
┌─────────────────┐        ┌────────────────┐
│ Pool validates  │        │ Continue mining│
│ • Accepted ✓    │        │ next nonce     │
│ • Rejected ✗    │        │ range          │
└─────────────────┘        └────────────────┘
```

### 3. Statistics Updates
```
Every 10 seconds:
• Calculate hash rate
• Count accepted/rejected shares
• Update uptime
• Trigger onStats callback
```

## Threading Model

```
Main Thread
├── Config loading
├── Initialization
└── Signal handling (Ctrl+C)

Network Thread
├── Stratum connection
├── Message parsing (JSON-RPC)
├── Job notifications
└── Difficulty updates

Mining Threads (1 per GPU)
├── CUDA kernel launches
├── Nonce distribution
├── Solution detection
└── Result collection

Stats Thread
├── Hash rate calculation
├── Performance monitoring
└── Periodic reporting
```

## Memory Management

### Host (CPU)
- **RAII pattern:** All resources in smart pointers
- **Pool allocators:** For frequent small allocations
- **Configuration:** Stack-allocated where possible

### Device (GPU)
- **DAG:** Persistent allocation (~4GB)
- **Work buffers:** Pinned memory for fast transfers
- **Solutions:** Page-locked memory for async copy
- **Streams:** Multiple CUDA streams for overlap

## Key Design Decisions

### 1. Pimpl Idiom
All major classes use pimpl to:
- Hide implementation details
- Reduce compile times
- Maintain ABI stability
- Separate CUDA code from C++ code

### 2. Zero-Copy Where Possible
- Pinned memory for host-device transfers
- Persistent DAG on GPU (no repeated transfers)
- Minimize synchronization points

### 3. Modular Architecture
Each component is independent:
- Can test in isolation
- Easy to replace implementations
- Clear interfaces between modules

### 4. Error Handling Strategy
- **CUDA errors:** Exceptions with detailed messages
- **Network errors:** Reconnection with backoff
- **Pool errors:** Log and continue mining
- **Critical errors:** Clean shutdown

## Performance Characteristics

### Expected Hash Rates
- **RTX 3080:** ~90-100 MH/s
- **RTX 3090:** ~120-130 MH/s
- **RTX 4090:** ~150-170 MH/s

### Memory Requirements
- **Per GPU:** 4-5 GB VRAM (DAG + working buffers)
- **Host:** ~500 MB RAM
- **DAG cache:** ~4 GB disk space per epoch

### CPU Usage
- Network thread: ~5%
- Stats thread: <1%
- Minimal CPU overhead

## Future Enhancements

### Planned Features
- [ ] Dual mining support
- [ ] Failover pools
- [ ] Web dashboard
- [ ] Temperature monitoring
- [ ] Auto-tuning intensity
- [ ] Stratum V2 support

### Optimization Opportunities
- [ ] OpenCL support (AMD GPUs)
- [ ] CPU mining (AVX2/AVX512)
- [ ] Better DAG caching
- [ ] Kernel auto-tuning
- [ ] Zero-copy networking
