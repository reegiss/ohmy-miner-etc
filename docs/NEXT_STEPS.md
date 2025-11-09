# Next Steps & Roadmap

**Current Status**: Phase 1 Complete ✅  
**Last Updated**: November 9, 2025  
**Next Phase**: Phase 2 - Async Memory Overlapping  

## Current Implementation Summary

- ✅ Dual-stream CUDA architecture implemented
- ✅ 8.14 MH/s baseline maintained (zero regressions)
- ✅ All 6 unit tests passing
- ✅ Pool connectivity verified (2+ minutes stable)
- ✅ Comprehensive documentation complete

## Phase 2: Async Memory Overlapping (Recommended Next)

**Timeline**: 2-3 hours  
**Difficulty**: Medium  
**Expected Improvement**: 5-10% GPU utilization increase

### What to Implement

```cpp
// Replace current blocking memcpy with async version
// in DeviceManager::Impl::search()

CUDA_CHECK(cudaMemcpyAsync(d_solutionCount_, &zero, sizeof(uint32_t), 
                           cudaMemcpyHostToDevice, stream_memory_));

CUDA_CHECK(cudaMemcpyAsync(d_header_, headerHash.data(), 32, 
                           cudaMemcpyHostToDevice, stream_memory_));

// Create synchronization event between streams
cudaEvent_t memoryDoneEvent;
CUDA_CHECK(cudaEventCreate(&memoryDoneEvent));
CUDA_CHECK(cudaEventRecord(memoryDoneEvent, stream_memory_));

// Compute stream waits for memory operations
CUDA_CHECK(cudaStreamWaitEvent(stream_compute_, memoryDoneEvent));

// Kernel launches on stream_compute_ after memory ready
```

### Testing Strategy

1. **Compile & Link**: Verify no errors
2. **Benchmark**: `./tests/bench_cuda` - should show 8-9 MH/s or better
3. **Unit Tests**: All 6 tests should still pass
4. **Pool Test**: 5-minute mining session with stability check
5. **Performance Compare**: Document improvement vs Phase 1

### Success Criteria

- ✅ No build errors
- ✅ Hashrate ≥ 8.14 MH/s (no regression)
- ✅ All tests pass
- ✅ Pool connection works
- ✅ Measured improvement in GPU utilization

### Potential Challenges

- **Event Management**: Must properly create/destroy events
- **Stream Ordering**: Ensure correct synchronization sequence
- **Timing**: Measure overhead of async operations

### Rollback Plan

If Phase 2 causes regressions:
```bash
git revert <commit-hash>  # Revert to Phase 1
```

---

## Phase 3: Three-Stream Pipeline (Advanced)

**Timeline**: 3-4 hours  
**Difficulty**: Hard  
**Expected Improvement**: 10-15% GPU utilization increase

### Architecture

```
stream_compute_     → GPU kernel execution
stream_memory_      → DAG/job data transfers  
stream_io (new)     → Network I/O processing (CPU context)
```

### Benefits

- Overlap kernel execution with DAG cache updates
- Network operations don't block kernel launching
- Continuous mining without synchronization stalls

### Implementation Notes

- May require changes to `miner_main.cpp` loop structure
- Need to handle job queueing for pipeline stages
- More complex event synchronization

---

## Phase 4: Callback-Based Results (Complex)

**Timeline**: 4-5 hours  
**Difficulty**: Very Hard  
**Expected Improvement**: Sub-millisecond job switching latency

### Approach

Instead of blocking on result memcpy:

```cpp
// Register callback to process results asynchronously
cudaHostFn_t hostFunc = [](void* data) {
    // Process solution data in background
    DeviceManager* dm = reinterpret_cast<DeviceManager*>(data);
    dm->processResults();  // Non-blocking
};

CUDA_CHECK(cudaLaunchHostFunc(stream_compute_, hostFunc, this));
```

### Benefits

- CPU never blocks on GPU operations
- Results processed while next job queued
- Maximum GPU utilization during transitions

### Risks

- Complex callback coordination
- Difficult to debug
- Potential race conditions if not careful

---

## Quick Reference: Next Session

### Before Starting Phase 2

```bash
# 1. Verify current state
cd /home/regis/develop/ohmy-miner-etc
git log --oneline -3
# Should show: 0263b55, c68a9b3

# 2. Create feature branch
git checkout -b feature/phase2-async-memcpy

# 3. Run baseline benchmark
./build/tests/bench_cuda 2>&1 | grep "Average Hashrate"
# Should show: ~8.01799 MH/s
```

### During Development

```bash
# Rebuild frequently
cd build && make -j$(nproc)

# Test after each change
./tests/bench_cuda | tail -20
ctest --verbose

# Pool testing
timeout 60 src/ohmy-miner-etc --pool stratum+tcp://us-etc.2miners.com:1010 \
  --wallet 0xe3c52bab8907c03b8305f9cd21d48a320de439b7.test-worker
```

### After Phase 2 Complete

```bash
# Commit
git add -A
git commit -m "feat: Implement async memcpy on stream_memory_

- Results: <measured improvement>
- Benchmark: <new hashrate>
- Tests: 6/6 PASS
- Pool verified: YES"

# Create pull request or merge
git checkout trunk
git merge feature/phase2-async-memcpy
```

---

## Performance Improvement Roadmap

```
Current:  8.14 MH/s  (baseline, Phase 1)
Phase 2:  8.40-9.00 MH/s  (estimated +3-10%)
Phase 3:  9.00-9.50 MH/s  (estimated +10-15% from Phase 2)
Phase 4:  9.50+ MH/s  (sub-ms latency, maximum utilization)

GPU Utilization:
Current:  ~94% (3-5ms idle per cycle)
Phase 2:  ~96% (1-2ms idle per cycle)
Phase 3:  ~98% (<1ms idle)
Phase 4:  ~99% (maximum possible)
```

---

## Documentation to Update

After each phase:

1. **docs/ASYNC_MULTISTREAM_OPTIMIZATION.md**
   - Add Phase results
   - Update architecture diagram
   - Record performance metrics

2. **docs/SESSION_ASYNC_MULTISTREAM.md** (or new session doc)
   - Document Phase 2-4 progress
   - Record lessons learned
   - Note any challenges

3. **docs/QUICK_REFERENCE.md**
   - Update if build commands change
   - Add optimization notes

---

## Known Limitations & Constraints

### GPU Memory
- GTX 1660 SUPER: 5739 MB total
- DAG: 4136 MB (epoch 778)
- Remaining: 1603 MB for operations
- **Constraint**: Cannot exceed available memory

### CUDA Architecture
- Target: sm_75 (GTX 1660 SUPER)
- Supported: sm_60 through sm_89
- **Constraint**: Must compile for all targets

### Network
- Pool: us-etc.2miners.com:1010 (stratum protocol)
- Job rate: ~1 new job per 5-10 seconds
- **Constraint**: Pool controls job frequency

### Performance Budget
- Current: 34ms cycle (kernel + overhead)
- Theoretical max: 32ms (kernel only)
- **Constraint**: Overhead ~2ms, difficult to reduce below 1ms

---

## Debugging Guide for Phase 2+

### Issue: Hashrate drops below 8.14 MH/s

**Debug Steps**:
```bash
# 1. Check build without optimization
cd build && make clean && make
./tests/bench_cuda | grep "Average Hashrate"

# 2. Check stream error handling
# Look for CUDA errors in output
# Verify cudaEventCreate/Destroy are paired

# 3. Revert and compare
git stash
./tests/bench_cuda | grep "Average Hashrate"
# Compare with new version
```

### Issue: GPU utilization still low

**Debug Steps**:
```bash
# 1. Monitor GPU during mining
nvidia-smi -i 0 dmon -c 30
# Should show 100% util most of the time

# 2. Check for synchronization points
# Look for cudaStreamSynchronize calls
# Verify only necessary ones remain

# 3. Profile with nsight
# nsys profile -t cuda,osrt -o profile ./test
```

### Issue: Tests fail sporadically

**Debug Steps**:
```bash
# 1. Run test multiple times
for i in {1..10}; do
  ./tests/bench_cuda > /tmp/test_$i.log
  grep "Average Hashrate" /tmp/test_$i.log
done
# Check for variance

# 2. Look for race conditions
# Ensure no shared state between streams

# 3. Verify event synchronization
# Add debug logging around cudaStreamWaitEvent
```

---

## Resources & References

### CUDA Documentation
- [CUDA Streams and Events](https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#streams-and-events)
- [CUDA Runtime API](https://docs.nvidia.com/cuda/cuda-runtime-api/index.html)
- [CUDA Memory Management](https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#memory-management)

### Project Documentation
- `docs/ARCHITECTURE.md` - GPU and kernel architecture
- `docs/ETHASH.md` - Algorithm implementation
- `docs/ASYNC_MULTISTREAM_OPTIMIZATION.md` - Current optimization

### Previous Sessions
- `docs/SESSION_ASYNC_MULTISTREAM.md` - This session summary
- `docs/SESSION_CLEANUP_AND_TESTING.md` - Code cleanup session
- `docs/WARP_KERNEL_ANALYSIS.md` - Warp-level kernel investigation

---

## Success Criteria for Each Phase

### Phase 2 ✅ (Async Memory)
- [ ] Hashrate ≥ 8.14 MH/s
- [ ] All tests pass
- [ ] Pool test stable 5+ mins
- [ ] GPU util shows improvement
- [ ] Documentation updated

### Phase 3 ⏳ (3-Stream)
- [ ] Hashrate ≥ 8.50 MH/s
- [ ] All tests pass
- [ ] No job switching delays visible
- [ ] GPU util 95%+
- [ ] Architecture diagram updated

### Phase 4 ⏳ (Callbacks)
- [ ] Hashrate ≥ 9.0 MH/s
- [ ] All tests pass
- [ ] Sub-1ms job switching
- [ ] GPU util 98%+
- [ ] Complete pipeline documented

---

## Summary

**Current State**: Solid foundation with dual-stream architecture  
**Recommended Next**: Phase 2 (async memcpy, 5-10% improvement expected)  
**Risk Level**: Low (can easily rollback if issues)  
**Estimated Value**: 10-30% overall GPU utilization improvement across all phases  
**Timeline**: 2-3 weeks for all phases (1 per session)  

Ready to proceed when you want to start Phase 2! 🚀
