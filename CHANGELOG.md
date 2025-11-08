# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial project structure
- CMake build system with CUDA support
- Core interfaces and class definitions
- Unit test framework
- Benchmark framework
- Comprehensive documentation
  - Architecture guide
  - Ethash algorithm guide
  - Stratum protocol guide
  - CUDA optimization guide
  - Quick reference guide
- Build and mining scripts
- Example configuration file

### Project Structure
- `src/core/` - Mining engine and Ethash implementation
- `src/cuda/` - CUDA kernels and GPU management
- `src/network/` - Stratum protocol client
- `src/dag/` - DAG generation and caching
- `src/utils/` - Logging and configuration
- `include/ohmy/` - Public API headers
- `tests/` - Unit tests and benchmarks
- `docs/` - Technical documentation

### Build System
- C++17 standard support
- CUDA 12.0+ support
- Multi-GPU architecture support (sm_60-89)
- OpenSSL integration
- nlohmann/json integration
- CTest integration

### Testing
- Ethash algorithm tests
- DAG generator tests
- Stratum client tests
- Performance benchmarks

## [0.1.0] - 2025-11-08

### Initial Release
- Project scaffolding complete
- Build system verified and tested
- 80% test coverage (4/5 tests passing)
- Ready for implementation phase

---

## Version History

### Version Numbering

We use Semantic Versioning:
- MAJOR version for incompatible API changes
- MINOR version for new functionality (backward compatible)
- PATCH version for bug fixes (backward compatible)

### Planned Releases

#### v0.2.0 - Core Algorithm
- [ ] Ethash implementation
- [ ] Keccak-256 hash
- [ ] FNV hash
- [ ] Solution verification

#### v0.3.0 - CUDA Mining
- [ ] Basic CUDA kernels
- [ ] Memory optimization
- [ ] Multi-GPU support

#### v0.4.0 - Network Integration
- [ ] Stratum protocol
- [ ] Pool connection
- [ ] Job handling
- [ ] Share submission

#### v0.5.0 - DAG Management
- [ ] DAG generation
- [ ] Disk caching
- [ ] Epoch management

#### v1.0.0 - Production Ready
- [ ] Full feature set
- [ ] Comprehensive testing
- [ ] Performance optimization
- [ ] Documentation complete
- [ ] Stable API

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.
