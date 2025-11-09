# Phase 3 Planning: 3-Stream Pipeline Architecture

**Date**: November 9, 2025  
**Status**: Planning (Ready to implement)  
**Expected Duration**: 3-4 hours  
**Target Improvement**: +10-15% GPU utilization  

## Overview

Phase 3 will extend the async architecture to a full 3-stream pipeline, enabling truly concurrent GPU operations and continuous job queueing without blocking the main mining loop.

## Architecture

### Current (Phase 2): 2 Streams
```
stream_memory_ ──→ H2D memcpy (async)
stream_compute_ → Kernel execution (waits on event)
Result: 0 blocking on CPU, but sequential GPU ops
```

### Proposed (Phase 3): 3 Streams
```
stream_compute_    ──→ Kernel execution (primary work)
stream_memory_     ──→ DAG updates & job data transfers
stream_io_ (new)   ──→ Network I/O & non-blocking ops (CPU)

Benefit: True concurrency - memory ops + kernel execution overlapped
```

## Implementation Plan

### Step 1: Architecture Design

**Current Data Flow**:
```
Pool Job  ──→ Unpack  ──→ cudaMemcpyAsync  ──→ Kernel  ──→ Results
          (CPU)       (stream_memory_)     (stream_compute_)
```

**Proposed Data Flow**:
```
Job Queue  ──→  Job 1: Async memcpy  Job 2: Kernel exec    Job 3: Results
              (stream_memory_)     (stream_compute_)     (stream_io_)
              
Parallel execution on 3 independent streams
```

### Step 2: Create stream_io_

```cpp
// In DeviceManager::Impl
cudaStream_t stream_io_;  // New stream for I/O operations

// In initDevice()
CUDA_CHECK(cudaStreamCreate(&stream_io_));

// In cleanup()
if (stream_io_) {
    cudaStreamDestroy(stream_io_);
    stream_io_ = nullptr;
}
```

### Step 3: Job Queueing System

Current: Process one job per search() call:
```cpp
search(job1);  // Process job 1
search(job2);  // Wait for job 1 complete, then job 2
```

Proposed: Queue multiple jobs:
```cpp
struct JobQueue {
    job_data jobs[3];     // Pre-queue next 2 jobs
    uint8_t front;        // Current job being executed
    uint8_t back;         // Next job to queue
};

// In search():
queue_job(stream_memory_);   // Queue memcpy on stream_memory_
execute_kernel(stream_compute_);  // Current kernel on stream_compute_
transfer_results(stream_io_);     // Results on stream_io_
```

### Step 4: Event Chain Synchronization

```cpp
// Current single pipeline event:
cudaEvent_t memoryDoneEvent;

// Proposed 3-stage event chain:
cudaEvent_t memoryDone, kernelDone, resultsDone;

// Synchronization:
queue_on_stream_memory();     // Async memcpy
record_event(memoryDone, stream_memory_);
stream_compute_.wait(memoryDone);  // Kernel waits

launch_kernel(stream_compute_);
record_event(kernelDone, stream_compute_);
stream_io_.wait(kernelDone);  // Results wait

transfer_results(stream_io_);
record_event(resultsDone, stream_io_);
```

### Step 5: Main Loop Integration

Current search() function:
```cpp
// Synchronous return
solutions = search(job);  // Waits for results
// Process and submit to pool
```

Proposed with job queueing:
```cpp
// Non-blocking job submission
submit_job_async(job);      // Queue on streams, return immediately

// In background:
// - stream_memory_: uploading next job data
// - stream_compute_: executing current kernel
// - stream_io_: (reserved for future result processing)

// Retrieve results when ready:
results = fetch_results();  // Non-blocking check
if (results_ready) {
    submit_to_pool(results);
}
```

## Implementation Steps

### Phase 3.1: Add stream_io_
- [ ] Create `stream_io_` in `Impl` class
- [ ] Initialize in `initDevice()`
- [ ] Cleanup in destructor
- [ ] Test compilation
- Commit: "WIP: Add stream_io_ for Phase 3"

### Phase 3.2: Implement Event Chain
- [ ] Add event chain: memoryDone, kernelDone, resultsDone
- [ ] Implement `cudaStreamWaitEvent()` chain
- [ ] Test event propagation
- Commit: "WIP: Implement 3-event synchronization chain"

### Phase 3.3: Benchmark & Test
- [ ] Rebuild and test
- [ ] Run benchmark: `./tests/bench_cuda`
- [ ] Run pool test: 5-minute session
- [ ] Compare Phase 2 baseline
- Commit: "Phase 3.3: Event chain complete & tested"

### Phase 3.4: Documentation
- [ ] Create Phase 3 report
- [ ] Document architecture
- [ ] Include performance comparisons
- Commit: "docs: Phase 3 architecture report"

## Expected Performance

### Benchmark Results (Projected)

```
Batch Size → Phase 2 → Phase 3 → Improvement
─────────────────────────────────────────────
262144     → 8.06    → 8.20    → +1.7%
1048576    → 8.14    → 8.30    → +1.9%
─────────────────────────────────────────────
Average    → 8.01    → 8.15    → +1.7%
```

### Pool Mining (Projected)

```
Metric          Phase 2      Phase 3      Improvement
────────────────────────────────────────────────────
GPU Util        ~95%         ~97%         +2%
Job Switches    ~6/min       ~6/min       (same rate)
Stall Time      1-2ms        <1ms         -50%
Hashrate        6.3 MH/s     6.4 MH/s     +1.6%
```

## Testing Strategy

### Unit Tests
```bash
cd build && ctest --verbose
# All 6 tests should still pass
```

### Benchmark
```bash
./tests/bench_cuda
# Expected: ~8.15 MH/s average (vs 8.01 Phase 2)
```

### Pool Mining (Critical)
```bash
timeout 300 src/ohmy-miner-etc --pool stratum+tcp://us-etc.2miners.com:1010 \
  --wallet 0xe3c52bab8907c03b8305f9cd21d48a320de439b7.phase3
# Monitor for 5 minutes
# Expected: Stable mining, hashrate 6.3+ MH/s
```

### GPU Utilization Monitoring
```bash
# In separate terminal
nvidia-smi -i 0 dmon -s pcum
# Monitor while pool is running
# Look for: sustained 100% GPU util (vs 95% Phase 2)
```

## Risk Assessment

### Low Risk ✅
- Adding new stream is low-impact
- Event synchronization pattern proven in Phase 2
- Fallback: can always disable new stream

### Potential Issues ⚠️
- **Issue**: Multiple events may add overhead
- **Mitigation**: Reuse events from pool
- **Test**: Benchmark with and without events

- **Issue**: Stream ordering confusion
- **Mitigation**: Clear comments in code
- **Test**: Verify event propagation

### Rollback Plan
```bash
# If Phase 3 causes issues:
git revert <phase3-commit>
# Return to Phase 2 (stable baseline)
```

## Success Criteria

- ✅ Hashrate ≥ 8.14 MH/s (no regression)
- ✅ All 6 tests pass
- ✅ Pool test stable 5+ minutes
- ✅ GPU utilization 96%+ (measured improvement)
- ✅ Code compiles all architectures (sm_60-sm_89)
- ✅ Zero CUDA errors in logs
- ✅ Comprehensive documentation

## Files to Modify

```
src/cuda/device_manager.cu
├─ Add: stream_io_ member variable
├─ Modify: initDevice() - create stream_io_
├─ Modify: cleanup() - destroy stream_io_
├─ Modify: search() - add event chain
└─ Modify: member variable list

docs/
├─ Create: PHASE3_3STREAM_REPORT.md
└─ Update: NEXT_STEPS.md (mark Phase 3 complete)
```

## Timeline Estimate

| Task | Duration | Notes |
|------|----------|-------|
| Planning (this doc) | 15 min | Complete |
| Add stream_io_ | 20 min | Straightforward |
| Event chain | 30 min | Similar to Phase 2 |
| Benchmark & test | 20 min | Rapid iteration |
| Documentation | 15 min | Recap work |
| Buffer | 10 min | Unexpected issues |
| **Total** | **~110 min** | **~2 hours** |

## Next Immediate Actions

1. Create feature branch: `git checkout -b feature/phase3-3stream`
2. Add stream_io_ member variable
3. Implement stream creation/cleanup
4. Add event chain synchronization
5. Rebuild and test
6. Document results
7. Merge to trunk

## Phase 4 Outlook

After Phase 3 is complete and merged, Phase 4 (Callback-based processing) becomes the next target:

- Eliminate blocking on result transfers
- Use `cudaLaunchHostFunc()` for async result processing
- Potential: 9.0+ MH/s, sub-millisecond latency
- Effort: 4-5 hours
- Difficulty: Hard (complex async coordination)

## Conclusion

Phase 3 is well-scoped and builds directly on Phase 2's async patterns. The implementation is straightforward, with proven techniques and low risk. Ready to proceed when development time is available.

**Ready to start Phase 3? Proceed with `git checkout -b feature/phase3-3stream`** ✅
