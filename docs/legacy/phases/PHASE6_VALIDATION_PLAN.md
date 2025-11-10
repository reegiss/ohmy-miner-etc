# Phase 6 Validation & Next Iteration Plan

## Current Status
- ✅ Phase 6 async pipeline fully implemented
- ✅ Triple-buffering with pinned host memory
- ✅ Non-blocking searchAsync() with cudaEventQuery
- ✅ Clean compilation, all tests passing
- ✅ Merged to trunk (commit d490ffe)

## Immediate Validation Tasks

### 1. Performance Measurement
```bash
# Baseline from previous: 8.14 MH/s
# Target: >9.0 MH/s (minimum +10%)
# Target: >10.0 MH/s (likely +15-20%)

# Run: ./build/src/ohmy-miner-etc --help
# Expected: Help output (binary works)

# Monitor GPU with: nvidia-smi -l 1 dmon -s puc
# Expected: GPU util 95-100% constant (vs 60-70% before)
```

### 2. Pool Integration Test
```bash
# Test pool connectivity and share submission
./build/src/ohmy-miner-etc \
  --pool eu1-etc.ethermine.org:4444 \
  --wallet 0x1234567890123456789012345678901234567890 \
  --worker testworker \
  --verbose

# Expected:
# - Connect to pool successfully
# - Receive mining job
# - Start searching with searchAsync
# - Submit shares without rejection
```

### 3. Monitoring with nvidia-smi
```bash
# Window 1: Monitor GPU every 1 second
watch -n 1 'nvidia-smi dmon -s puc'

# Expected metrics:
# - GPU Util: 95-100% (sustained, no drops)
# - Memory: Increasing slightly (pinned host buffers)
# - SM Clock: Maintained (no throttling)
```

## Performance Optimization Options

### Phase 6.1: Dynamic Stream Count
```cpp
// Current: NUM_STREAMS = 3 (fixed)
// Optimal: Adjust based on GPU memory pressure

// Calculate optimal streams:
// Available GPU memory / (DAG + buffers_per_stream) = N_STREAMS

// Benefits:
// - Larger GPUs (8GB+): use 4-5 streams for more overlap
// - Smaller GPUs (2GB): use 2 streams if memory tight
// - Automatic tuning based on hardware
```

### Phase 6.2: Concurrent DMA Channels
```cpp
// Use CUDA graph for unified work submission
// Instead of:
//   - HtoD on stream[i]
//   - Kernel on stream[i]
//   - DtoH on stream[i]

// Use graph:
//   - Node 1: HtoD
//   - Node 2: Kernel (waits for Node 1)
//   - Node 3: DtoH (waits for Node 2)
// - Replay with cudaGraphLaunch (overhead reduction)

// Benefits:
// - Reduce CUDA API call overhead
// - Better scheduling by CUDA runtime
// - Expected: +2-3% improvement
```

### Phase 6.3: Kernel Tuning per Stream
```cpp
// Current: Same kernel settings for all streams
// Optimize: Vary NONCES_PER_THREAD based on stream state

// Strategy:
// - Stream 0 (just launched): Use standard kernel
// - Stream 1 (mid-execution): Use optimized kernel
// - Stream 2 (about to complete): Use high-throughput variant

// Potential gain: +3-5%
```

### Phase 6.4: Event Callback Optimization
```cpp
// Current: Busy-wait in searchAsync (cudaEventQuery loop)
// Optimize: Use cudaHostRegister for faster DtoH

// Alternative approach:
// - Pre-map host memory with cudaHostRegister
// - Use GPU->CPU callbacks for results
// - Reduce CPU <-> GPU synchronization overhead

// Expected: +2-3% from reduced sync overhead
```

## Potential Issues & Mitigations

### Issue 1: Memory Pressure
**Problem**: Triple-buffering might exhaust small GPU memory
**Solution**: Make NUM_STREAMS configurable via env var
```cpp
const char* streamsEnv = std::getenv("OHMY_NUM_STREAMS");
int num_streams = streamsEnv ? std::atoi(streamsEnv) : 3;
```

### Issue 2: Context Switch Overhead
**Problem**: Round-robin might cause unnecessary GPU context switches
**Mitigation**: Ensure streams created on same context (already done)
**Monitor**: nvidia-smi to verify no context switches

### Issue 3: Pinned Memory Fragmentation
**Problem**: Multiple cudaHostAlloc calls might fragment memory
**Solution**: Single allocation with pointer arithmetic (Phase 6.5)

## Testing Scenarios

### Scenario 1: Short Burst (30 seconds)
```
Goal: Verify non-blocking behavior
- Launch searchAsync
- Measure: responses per second
- Expected: >1000 calls/sec (no blocking)
```

### Scenario 2: Long Run (5 minutes)
```
Goal: Monitor for memory leaks, GPU errors
- Run on pool
- Monitor: GPU memory, error counts
- Expected: Stable memory, zero CUDA errors
```

### Scenario 3: Job Change During Pipeline
```
Goal: Verify correct handling when job changes mid-pipeline
- Connect to pool
- Trigger job change
- Verify: Solutions discarded correctly, new job processed
```

### Scenario 4: Multi-GPU (if available)
```
Goal: Verify each GPU gets independent pipeline
- Initialize all GPUs
- Monitor: Each GPU independent searchAsync calls
- Expected: Linear scaling (if 2 GPUs → 2x hashrate)
```

## Success Criteria

| Metric | Before Phase 6 | Target | Achieved |
|--------|---------------|--------|----------|
| Hashrate (GTX 1660S) | 8.14 MH/s | >9.0 MH/s | ? |
| GPU Utilization | 60-70% | 95-100% | ? |
| CPU Loop Rate | ~100 Hz | ~10000 Hz | ? |
| Share Submission Rate | Steady | No change | ? |
| Memory Stability | Baseline | No leaks | ? |

## Next Phase Ideas (Phase 7+)

### Phase 7: Multi-GPU Async Pipelines
- Independent searchAsync per GPU
- Aggregate statistics across pipelines
- Load balancing if GPUs differ

### Phase 8: Job Pipelining
- Prefetch next job while current runs
- Reduce job-change latency
- Predict job transitions

### Phase 9: Dynamic Difficulty Adjustment
- Monitor GPU utilization
- Adjust search range if <95%
- Maximize utilization per GPU

### Phase 10: CUDA Graph Integration
- Reduce API call overhead
- Pre-built work graphs
- Faster iteration cycle

## Immediate Next Steps

1. **Verify compilation** - ✅ Done (d490ffe)
2. **Run unit tests** - ✅ Done (7/7 passing)
3. **Create benchmarking script** - TODO
4. **Validate hashrate** - TODO
5. **Monitor GPU utilization** - TODO
6. **Test on real pool** - TODO
7. **Measure API call overhead** - TODO
8. **Optimize Phase 6.1-6.4** - TODO (based on results)

## Documentation to Generate

- [ ] Benchmarking results with before/after comparison
- [ ] GPU utilization graphs
- [ ] Performance analysis report
- [ ] Tuning recommendations for Phase 7
- [ ] API overhead profiling results

---

**Prepared**: November 9, 2025  
**Current Branch**: trunk (d490ffe)  
**Status**: Ready for validation & optimization iterations
