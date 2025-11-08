# OhMy Miner ETC

High-performance Ethereum Classic (ETC) GPU miner built from scratch with C++ and CUDA.

## Features

- ⚡ Optimized CUDA kernels for maximum hash rate
- 🔧 Support for multiple NVIDIA GPUs
- 🌐 Stratum protocol for pool mining
- 💾 DAG caching for faster startup
- 📊 Real-time statistics and monitoring
- 🛠️ Modern C++17 codebase with RAII patterns

## Requirements

### Hardware
- NVIDIA GPU with CUDA Compute Capability 6.0+ (Pascal or newer)
- Minimum 4GB VRAM per GPU
- Modern CPU with multi-threading support

### Software
- CUDA Toolkit 11.0 or later
- CMake 3.18 or later
- GCC 9+ or Clang 10+
- OpenSSL development libraries
- nlohmann/json (header-only)

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/ohmy-miner-etc.git
cd ohmy-miner-etc

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)
```

### Build Options

```bash
# Debug build with GPU debugging symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build with specific CUDA architectures
cmake .. -DCMAKE_CUDA_ARCHITECTURES="75;86;89"

# Build with documentation
cmake .. -DBUILD_DOCS=ON
```

## Usage

### Basic Usage

```bash
./ohmy-miner-etc --pool stratum+tcp://pool.example.com:4444 --wallet 0xYourWalletAddress
```

### Advanced Options

```bash
./ohmy-miner-etc \
  --pool stratum+tcp://pool.example.com:4444 \
  --wallet 0xYourWalletAddress \
  --worker miner01 \
  --devices 0,1,2 \
  --threads 2 \
  --log /var/log/miner.log
```

### Configuration File

Create `config.json`:

```json
{
  "pool": {
    "url": "stratum+tcp://pool.example.com:4444",
    "wallet": "0xYourWalletAddress",
    "worker": "miner01",
    "use_tls": false
  },
  "devices": {
    "gpu_ids": [0, 1, 2],
    "threads_per_gpu": 2,
    "intensity": 21
  },
  "general": {
    "log_file": "/var/log/ohmy-miner.log",
    "log_level": "INFO",
    "dag_cache_dir": "/tmp/dag-cache",
    "benchmark": false
  }
}
```

Then run:

```bash
./ohmy-miner-etc --config config.json
```

## Performance Tuning

### GPU Selection

Mine only on specific GPUs:
```bash
--devices 0,2,3  # Use GPUs 0, 2, and 3
```

### Intensity

Adjust mining intensity (higher = more GPU usage):
```bash
--intensity 21  # Range: 8-25, default: 21
```

### DAG Caching

Enable DAG caching for faster restarts:
```bash
--dag-cache /path/to/cache/dir
```

## Development

### Running Tests

```bash
cd build
ctest --verbose

# Run specific test suites
./tests/unit_tests
./tests/benchmark_tests
```

### Code Style

This project follows modern C++ best practices:
- C++17 standard
- RAII for resource management
- Smart pointers over raw pointers
- Const correctness
- Exception-based error handling

### CUDA Guidelines

- Optimize memory access patterns (coalesced reads)
- Use shared memory for frequently accessed data
- Check all CUDA API calls for errors
- Profile with NVIDIA Nsight Compute

## Architecture

```
src/
├── core/           # Mining engine and Ethash implementation
│   ├── miner.cpp
│   └── ethash.cpp
├── cuda/           # CUDA kernels and GPU management
│   ├── device_manager.cu
│   └── kernels/
│       ├── ethash_kernel.cu
│       └── search_kernel.cu
├── network/        # Stratum protocol client
│   ├── stratum_client.cpp
│   └── connection.cpp
├── dag/            # DAG generation and caching
│   └── dag_generator.cpp
└── utils/          # Logging, configuration, helpers
    ├── logger.cpp
    └── config.cpp
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

## License

MIT License - see [LICENSE](LICENSE) file for details

## Acknowledgments

- Ethereum Classic development team
- CUDA programming community
- ethminer project for reference implementation

## Support

- GitHub Issues: https://github.com/yourusername/ohmy-miner-etc/issues
- Discord: [Your Discord Link]
- Documentation: [Your Docs Link]

## Disclaimer

Mining cryptocurrency requires significant computational resources and electricity. Always ensure you:
- Have adequate cooling for your GPUs
- Monitor power consumption
- Comply with local regulations
- Use at your own risk
