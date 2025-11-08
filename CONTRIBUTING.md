# Contributing to OhMy Miner ETC

Thank you for your interest in contributing! This document provides guidelines for contributing to the project.

## Development Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/ohmy-miner-etc.git
   cd ohmy-miner-etc
   ```

2. **Install dependencies:**
   ```bash
   # Ubuntu/Debian
   sudo apt-get install build-essential cmake libssl-dev
   
   # Install CUDA Toolkit (if not already installed)
   # Download from: https://developer.nvidia.com/cuda-downloads
   ```

3. **Build the project:**
   ```bash
   ./scripts/build.sh
   ```

4. **Run tests:**
   ```bash
   cd build
   ctest --verbose
   ```

## Code Style

### C++ Guidelines

- Use **C++17** features
- Follow **RAII** principles
- Use **smart pointers** (unique_ptr, shared_ptr)
- Apply **const correctness**
- Use **meaningful variable names**
- Add **documentation comments** for public APIs

Example:
```cpp
namespace ohmy {

/**
 * @brief Calculate Ethash DAG size for given epoch
 * @param epoch Epoch number
 * @return DAG size in bytes
 */
uint64_t calculateDagSize(uint32_t epoch);

} // namespace ohmy
```

### CUDA Guidelines

- Separate CUDA kernels in `.cu` files
- Use `__device__`, `__host__`, `__global__` appropriately
- Optimize memory access patterns
- Check all CUDA errors
- Document kernel launch parameters

Example:
```cuda
/**
 * @brief Ethash search kernel
 * @param dag Pointer to DAG in device memory
 * @param dagSize Size of DAG in elements
 * @param headerHash Block header hash
 * @param startNonce Starting nonce value
 * @param target Difficulty target
 */
__global__ void search_kernel(
    const uint64_t* dag,
    uint64_t dagSize,
    const uint8_t* headerHash,
    uint64_t startNonce,
    uint64_t target
);
```

## Commit Messages

Follow the conventional commits format:

```
type(scope): subject

body (optional)

footer (optional)
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting)
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `test`: Test additions or changes
- `chore`: Build process or auxiliary tool changes

Examples:
```
feat(cuda): implement optimized search kernel

- Add coalesced memory access
- Use shared memory for DAG caching
- Improve occupancy to 85%

Closes #123
```

```
fix(stratum): handle connection timeout correctly

The previous implementation didn't properly handle timeout
when connecting to pools. Now implements exponential backoff.
```

## Pull Request Process

1. **Create a feature branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes and test:**
   ```bash
   # Make changes
   ./scripts/build.sh
   cd build && ctest
   ```

3. **Commit your changes:**
   ```bash
   git add .
   git commit -m "feat(component): description"
   ```

4. **Push to your fork:**
   ```bash
   git push origin feature/your-feature-name
   ```

5. **Create a Pull Request:**
   - Provide clear description
   - Reference any related issues
   - Include test results
   - Add screenshots if UI changes

## Testing Requirements

- All new features must include tests
- Maintain or improve test coverage
- Run all tests before submitting PR
- Add benchmarks for performance-critical code

## Areas for Contribution

### High Priority
- [ ] Implement Ethash algorithm (CPU version)
- [ ] Implement CUDA mining kernels
- [ ] Implement Stratum protocol client
- [ ] Implement DAG generation

### Medium Priority
- [ ] Add failover pool support
- [ ] Improve error handling
- [ ] Add temperature monitoring
- [ ] Optimize memory usage

### Low Priority
- [ ] Web dashboard
- [ ] Docker container
- [ ] Additional pool protocols
- [ ] AMD GPU support (OpenCL)

## Performance Optimization

When contributing performance improvements:

1. **Benchmark before and after:**
   ```bash
   ./build/tests/bench_ethash
   # Make changes
   ./scripts/build.sh
   ./build/tests/bench_ethash
   ```

2. **Profile with NVIDIA tools:**
   ```bash
   ncu --set full -o profile ./build/src/ohmy-miner-etc
   nsys profile ./build/src/ohmy-miner-etc
   ```

3. **Document the improvement:**
   - Include benchmark results
   - Explain optimization technique
   - Note any trade-offs

## Bug Reports

When reporting bugs, include:

1. **System information:**
   - OS version
   - CUDA version
   - GPU model
   - Driver version

2. **Steps to reproduce**

3. **Expected vs actual behavior**

4. **Logs and error messages**

5. **Configuration file (if applicable)**

Example:
```
### Bug Report

**Environment:**
- OS: Ubuntu 22.04
- CUDA: 12.0
- GPU: RTX 3080
- Driver: 525.125.06

**Steps to reproduce:**
1. Run `./build/src/ohmy-miner-etc --pool ...`
2. Wait 5 minutes
3. Observe crash

**Expected:** Miner continues running
**Actual:** Segmentation fault

**Logs:**
```
[ERROR] CUDA error: out of memory
Stack trace...
```
```

## Code Review

All submissions require review. We look for:

- Code quality and style
- Test coverage
- Documentation
- Performance impact
- Security considerations

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

## Questions?

- Open an issue for discussion
- Join our Discord (link)
- Check documentation in `docs/`

Thank you for contributing to OhMy Miner ETC! 🚀
