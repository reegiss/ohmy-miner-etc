# OhMy Miner ETC - Documentation Hub

Central access point for architecture, optimization reports, protocol references, and session histories.

For a categorized list of every markdown document, see [INDEX.md](./INDEX.md).

## Essential Documentation

### [ARCHITECTURE.md](./ARCHITECTURE.md)
Comprehensive overview of the project architecture, including:
- Project structure and directory layout
- Core components and their responsibilities
- Data flow and communication patterns
- GPU memory management and DAG caching
- Network layer (Stratum protocol)

### [ETHASH.md](./ETHASH.md)
Complete guide to the Ethash algorithm implementation:
- Algorithm overview and history
- ETC-specific modifications (ECIP-1099)
- DAG generation and caching strategy
- Implementation details in C++

### [STRATUM.md](./STRATUM.md)
Stratum pool protocol documentation:
- JSON-RPC 2.0 protocol specification
- Mining pool communication flow
- Authentication and job management
- Share submission and difficulty adjustment

### [QUICK_REFERENCE.md](./QUICK_REFERENCE.md)
Quick start guide for developers:
- Build commands
- Common development tasks
- Debugging and profiling
- Performance optimization workflow

---

## Investigation & Analysis Documentation

These documents represent significant research into optimization approaches and their outcomes:

### [WARP_KERNEL_ANALYSIS.md](./WARP_KERNEL_ANALYSIS.md)
**Status**: REJECTED - Architecture produces worse performance

Detailed technical analysis of warp-level cooperative kernel design:
- Architecture mismatch explanation
- Performance regression analysis (-54.6%)
- Root causes of synchronization overhead
- Architectural lessons learned
- Recommendations for future optimization

**Value**: Understanding why certain GPU optimization patterns fail and when they're applicable.

### [SESSION_SUMMARY_WARP_INVESTIGATION.md](./SESSION_SUMMARY_WARP_INVESTIGATION.md)
**Status**: Investigation complete

Comprehensive session summary covering:
- Warp kernel implementation details
- Refactored Keccak function architecture
- Build system integration
- Performance benchmarking results
- Key findings and decisions

### [TEXTURE_MEMORY_RESULTS.md](./TEXTURE_MEMORY_RESULTS.md)
**Status**: REJECTED - Architecture blocker (2GB buffer size limit)

Investigation of CUDA texture memory for DAG access:
- Synthetic benchmark results (+226% gain in 64KB DAG)
- Production DAG failure analysis (4GB exceeds 2GB limit)
- Root cause: CUDA linear texture memory architecture constraint
- Recommendation: Focus on other optimization approaches

**Value**: Understanding hardware limitations and when to reject optimization attempts early.

---

## Performance Summary

**Current Status**: 8.14 MH/s (GTX 1660 SUPER)
- Baseline kernel: 8.09 MH/s
- Optimized kernel (4 nonces/thread): 8.14 MH/s (+0.5%)
- Texture memory (rejected): BLOCKED at production scale
- Warp-level kernel (rejected): -54.6% regression

**Next Optimization Paths** (in priority order):
1. Higher batching (8-16 nonces/thread if registers allow)
2. Async multi-stream pipeline (+10-15% expected)
3. Register pressure optimization
4. Memory access pattern tuning

---

## Document Maintenance Guidelines

- **Keep**: Architecture, implementation guides, protocol specs, quick references
- **Remove**: Obsolete planning documents, superseded analysis, outdated benchmarks
- **Archive**: Failed optimization attempts (valuable for understanding failure modes)
- **Update**: Performance results when new milestones are achieved

---

## Key Takeaways

1. **Batching independent work** (4 nonces/thread) is the current winning strategy
2. **GPU architecture constraints** are real - texture memory 2GB limit is hard blocker
3. **Synchronization overhead** from inter-thread cooperation exceeds latency hiding benefits
4. **Register-based computation** beats shared memory for embarrassingly parallel workloads
5. **Incremental benchmarking** is essential - test one optimization at a time

---

Last updated: 2025-11-09
