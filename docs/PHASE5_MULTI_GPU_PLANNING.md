# Phase 5: Multi-GPU Support - Architecture & Planning

**Status:** 🚀 Planning Phase  
**Start Date:** November 9, 2025  
**Timeline:** 2-3 weeks  
**Goal:** Extend ohmy-miner-etc to support multiple GPU devices with independent mining threads and per-device callbacks

---

## Executive Summary

Phase 5 extends the single-GPU mining engine (Phase 4) to support multiple NVIDIA GPUs operating independently while sharing a single Stratum pool connection. Each GPU maintains:

- Independent CUDA streams and 3-stream async pipeline
- Per-device mining thread with isolated nonce ranges
- Per-device result callbacks asynchronously submitted to pool
- Real-time hashrate aggregation across all devices
- Optimal load distribution with zero nonce collision

**Key Architecture Change:**

```
Phase 4 (Single GPU):
┌─────────────────────────────────────────────────┐
│ Main Thread                                      │
│  ├─ Pool Connection (StratumClient)             │
│  └─ GPU #0 Mining                               │
│     ├─ Stream Pipeline (compute/memory/io)      │
│     └─ Result Callback → Pool Submission        │
└─────────────────────────────────────────────────┘

Phase 5 (Multi-GPU):
┌────────────────────────────────────────────────────────────────┐
│ Main Thread                                                     │
│  ├─ Pool Connection (StratumClient - SHARED)                   │
│  └─ Device Manager                                             │
│     ├─ Mining Thread #0 (GPU #0)                               │
│     │  ├─ Stream Pipeline (compute/memory/io)                  │
│     │  ├─ Nonce Range: [0, N)                                  │
│     │  └─ Callback → Pool Submission                           │
│     ├─ Mining Thread #1 (GPU #1)                               │
│     │  ├─ Stream Pipeline (compute/memory/io)                  │
│     │  ├─ Nonce Range: [N, 2N)                                 │
│     │  └─ Callback → Pool Submission                           │
│     └─ Mining Thread #2 (GPU #2)                               │
│        ├─ Stream Pipeline (compute/memory/io)                  │
│        ├─ Nonce Range: [2N, 3N)                                │
│        └─ Callback → Pool Submission                           │
└────────────────────────────────────────────────────────────────┘
```

---

## Architecture Overview

### 1. Device Enumeration & Initialization

**Current State (Phase 4):**
- Single GPU hardcoded in DeviceManager
- One DAG cache, one stream pipeline
- One result callback handler

**Phase 5 Changes:**
- Auto-detect all NVIDIA GPUs in system via `cudaGetDeviceCount()`
- Initialize each GPU independently:
  - Set active device via `cudaSetDevice(deviceId)`
  - Allocate device memory (DAG cache, mining buffers)
  - Create 3-stream pipeline per device
  - Allocate host pinned memory for results
- Maintain per-device state in vector/map structures
- Track VRAM availability per GPU for DAG fitting

**New Methods in DeviceManager:**

```cpp
// Enumerate and initialize all available GPUs
std::vector<int> initializeAllDevices();

// Get device info (memory, compute capability, name)
DeviceInfo getDeviceInfo(int deviceId) const;

// Initialize specific device
void initializeDevice(int deviceId);

// Release resources for specific device
void shutdownDevice(int deviceId);

// Get count of active devices
int getDeviceCount() const;

// Check if device is available
bool isDeviceAvailable(int deviceId) const;
```

### 2. Per-Device Mining Threads

**Current State (Phase 4):**
- Single linear search loop in main/primary thread
- Blocking until done or external stop signal

**Phase 5 Changes:**
- Create independent mining thread per active GPU
- Each thread:
  - Calls `cudaSetDevice(deviceId)` at start
  - Runs isolated search loop on assigned GPU
  - Handles CUDA errors independently
  - Stores per-device hashrate statistics
  - Synchronizes job updates via atomic flags/shared context
- Main thread acts as orchestrator:
  - Coordinates job distribution
  - Aggregates metrics
  - Handles pool reconnection
  - Manages thread lifecycle

**Thread Coordination:**

```cpp
struct MiningJobContext {
    std::string jobId;
    uint32_t epoch;
    std::atomic<bool> isValid{false};
    std::atomic<uint64_t> timestamp{0};
    std::mutex jobMutex;
    
    // Used by all GPU threads
    std::vector<uint8_t> jobData;
};

class DeviceManager {
private:
    std::vector<std::thread> minerThreads_;
    std::shared_ptr<MiningJobContext> jobContext_;
    std::vector<PerDeviceMetrics> metrics_;
    std::atomic<bool> running_{false};
};

struct PerDeviceMetrics {
    int deviceId;
    uint64_t hashesComputed{0};
    std::chrono::high_resolution_clock::time_point lastUpdate;
    double instantHashrate{0.0};
    uint64_t solutionsFound{0};
    std::atomic<uint32_t> errors{0};
};
```

### 3. Nonce Distribution Strategy

**Challenge:** Avoid nonce collisions across multiple GPUs while maintaining optimal throughput

**Solution: Partitioned Nonce Range**

For a 32-bit nonce space (0 to 2^32-1):
- Divide total nonce space into N equal-sized ranges (N = number of GPUs)
- GPU #i operates on range: `[i * (2^32/N), (i+1) * (2^32/N))`
- Each GPU exhausts its range independently
- On job change, reset to new range partition

**Example for 4 GPUs:**
```
GPU 0: nonce ∈ [0x00000000, 0x40000000)              (1 billion nonces)
GPU 1: nonce ∈ [0x40000000, 0x80000000)              (1 billion nonces)
GPU 2: nonce ∈ [0x80000000, 0xC0000000)              (1 billion nonces)
GPU 3: nonce ∈ [0xC0000000, 0xFFFFFFFF]              (1 billion nonces)
```

**Implementation in search() loop:**

```cpp
void DeviceManager::Impl::searchOnDevice(int deviceId, 
                                         const MiningJobContext& jobCtx,
                                         uint64_t startNonce) {
    cudaSetDevice(deviceId);
    
    uint32_t rangeSize = UINT32_MAX / deviceCount_;
    uint32_t deviceOffset = deviceId * rangeSize;
    uint32_t rangeEnd = (deviceId + 1) * rangeSize;
    
    for (uint32_t nonce = deviceOffset; nonce < rangeEnd; nonce += NONCES_PER_KERNEL) {
        // Launch kernel with adjusted nonce offset
        ethashSearch<<<grid, block, 0, computeStream_>>>(
            nonce,  // Per-GPU adjusted starting nonce
            jobCtx.jobData.data(),
            ...
        );
        
        // Handle results via callback (already async)
    }
}
```

### 4. Per-Device Result Callbacks

**Current State (Phase 4):**
- Single ResultCallbackData structure
- One async callback handler
- Results submitted directly to StratumClient

**Phase 5 Changes:**
- Extend ResultCallbackData to track source device:
  ```cpp
  struct ResultCallbackData {
      int deviceId;  // ← NEW: which GPU found this
      std::vector<Solution> solutions;
      void* stratumClient;
      std::string jobId;
      uint32_t epoch;
      uint64_t timestamp;  // ← NEW: for latency tracking
  };
  ```

- Update callback handler:
  ```cpp
  void processAndSubmitResultsCallback(cudaStream_t stream, 
                                       cudaError_t error, 
                                       void* userData) {
      auto* data = static_cast<ResultCallbackData*>(userData);
      
      LOG_DEBUG("[ResultCallback Device #" + std::to_string(data->deviceId) + 
               "] Processing " + std::to_string(data->solutions.size()) + 
               " solutions");
      
      // Submit to pool (thread-safe via StratumClient mutex)
      for (const auto& sol : data->solutions) {
          auto client = static_cast<StratumClient*>(data->stratumClient);
          client->submitSolution(sol.nonce, data->jobId, data->epoch);
      }
      
      delete data;
  }
  ```

- **Thread-Safety:** StratumClient::submitSolution() must use mutex internally

### 5. Hashrate Aggregation

**Current State (Phase 4):**
- Single device hashrate via `getHashRate(0)`
- Linear calculation

**Phase 5 Changes:**
- Per-device hashrate tracking:
  ```cpp
  double DeviceManager::getHashRate(int deviceId) const;  // ← per-GPU
  double DeviceManager::getTotalHashRate() const;          // ← aggregate
  std::vector<double> DeviceManager::getAllHashRates() const;  // ← all GPUs
  ```

- Calculation per device:
  ```cpp
  double hashrate = (double)metrics[i].hashesComputed / 
                    (elapsed_time_seconds);
  ```

- Thread-safe updates via atomic variables or mutex-protected metrics map

### 6. Job Update Synchronization

**Challenge:** Broadcast new job to all GPU threads without interrupting mining

**Solution: Atomic Job Context with Copy-on-Write**

```cpp
void DeviceManager::setMiningJobContext(const std::string& jobId, 
                                       uint32_t epoch) {
    // Create new job context
    auto newCtx = std::make_shared<MiningJobContext>();
    newCtx->jobId = jobId;
    newCtx->epoch = epoch;
    newCtx->timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    newCtx->isValid.store(true);
    
    // All GPU threads polling jobContext_ will see update
    {
        std::lock_guard<std::mutex> lock(jobContextMutex_);
        jobContext_ = newCtx;
    }
    
    // GPU threads check periodically:
    if (lastJobContext_ != jobContext_) {
        lastJobContext_ = jobContext_;
        // Update nonce range, reset counters
    }
}
```

### 7. StratumClient Thread-Safety

**Current State (Phase 4):**
- StratumClient called from single callback thread
- Callback thread per GPU (in Phase 5)

**Phase 5 Changes:**
- Multiple callback threads may call StratumClient::submitSolution() concurrently
- **Required Changes:**
  1. Add mutex to StratumClient for submitSolution()
  2. Ensure send() and receive() don't deadlock with multiple threads
  3. Test concurrent submission from multiple GPUs
  4. Add per-submission atomic counter to avoid race conditions

**Example:**

```cpp
class StratumClient {
private:
    std::mutex submitMutex_;  // ← NEW
    
public:
    bool submitSolution(uint32_t nonce, 
                       const std::string& jobId, 
                       uint32_t epoch) {
        std::lock_guard<std::mutex> lock(submitMutex_);  // ← NEW
        
        // ... existing submit logic ...
        return sendMessage(msg);
    }
};
```

---

## Implementation Strategy

### Phase 5.1: Device Enumeration & Initialization
**Duration:** 3-4 days

1. Add GPU auto-detection to DeviceManager::initialize()
2. Create per-device state structures (streams, buffers, metrics)
3. Update CMakeLists.txt if needed
4. Write unit tests for device detection
5. Validate with multi-GPU test machine (or single GPU with future compatibility)

**Deliverable:** DeviceManager initializes all available GPUs

### Phase 5.2: Per-Device Mining Threads
**Duration:** 4-5 days

1. Create mining thread wrapper function
2. Implement thread spawning in DeviceManager::startMining()
3. Add thread synchronization primitives
4. Implement graceful shutdown with signal handling
5. Add per-thread error tracking
6. Write integration tests for thread lifecycle

**Deliverable:** Each GPU runs independent mining loop

### Phase 5.3: Nonce Distribution & Load Balancing
**Duration:** 2-3 days

1. Implement nonce range calculation per device
2. Update search() kernel launch with device-specific nonce offset
3. Add nonce collision detection (sanity check)
4. Benchmark throughput per device (should be ~equal)
5. Stress test for 24+ hours

**Deliverable:** Linear scaling across devices (or near-linear)

### Phase 5.4: Per-Device Callbacks
**Duration:** 2-3 days

1. Extend ResultCallbackData with deviceId field
2. Update result_callback.cu for per-device logging
3. Add device metrics accumulation
4. Write tests for concurrent callbacks
5. Validate no callback interference between devices

**Deliverable:** Each GPU submits solutions independently

### Phase 5.5: Thread-Safe Pool Integration
**Duration:** 2 days

1. Add mutex to StratumClient::submitSolution()
2. Test concurrent submissions from multiple GPUs
3. Validate pool accepts shares from multi-GPU submissions
4. Run 60+ second pool test with multiple GPUs
5. Monitor for submission conflicts or dropped solutions

**Deliverable:** Pool mining works with multi-GPU setup

### Phase 5.6: Comprehensive Testing & Validation
**Duration:** 3-4 days

1. Unit tests for device enumeration
2. Integration tests for multi-GPU initialization
3. Stress tests (48+ hours continuous mining)
4. Performance benchmarks on various GPU combinations
5. Pool mining validation with real shares
6. CUDA error tracking and reporting

**Deliverable:** All tests passing, zero regressions, stable operation

### Phase 5.7: Documentation & Deployment
**Duration:** 2-3 days

1. Write detailed architecture report
2. Document API changes and migration guide
3. Create performance comparison (single vs multi-GPU)
4. Write deployment guide for multi-GPU systems
5. Update README with multi-GPU examples
6. Create release notes

**Deliverable:** Comprehensive documentation, merge to trunk

---

## Technical Challenges & Solutions

### Challenge 1: CUDA Context Per Device
**Problem:** Each thread must call `cudaSetDevice(deviceId)` to get valid context
**Solution:** Thread-local context setting, validation checks in error handler

### Challenge 2: Concurrent Stratum Submissions
**Problem:** Multiple callback threads calling StratumClient simultaneously
**Solution:** Mutex protection on submitSolution(), serialize network I/O

### Challenge 3: Nonce Collision Risk
**Problem:** Different GPUs could theoretically check same nonce
**Solution:** Partitioned nonce ranges with non-overlapping offset calculations

### Challenge 4: Job Update Race Conditions
**Problem:** GPU thread updates job context while checking in another thread
**Solution:** Copy-on-write semantics, atomic updates, shared_ptr for thread-safe sharing

### Challenge 5: Hashrate Accuracy Across Threads
**Problem:** Race conditions in updating hashrate counters
**Solution:** Atomic variables for counters, periodic snapshot for display

### Challenge 6: DAG Memory on Multiple GPUs
**Problem:** DAG is epoch-dependent, large (3+ GB for recent epochs)
**Solution:** Each GPU allocates independent DAG copy (expected), or use peer access (advanced)

---

## Success Criteria

### Functional Requirements
- ✅ Auto-detects all NVIDIA GPUs in system
- ✅ Initializes each GPU independently with proper context
- ✅ Spawns independent mining thread per GPU
- ✅ Each GPU mines with isolated nonce ranges (no collisions)
- ✅ Per-GPU callbacks submit solutions asynchronously
- ✅ Stratum pool accepts solutions from all GPUs
- ✅ Job updates propagate to all mining threads
- ✅ Graceful shutdown of all threads

### Performance Requirements
- ✅ Near-linear hashrate scaling (N GPUs ≈ N × single-GPU hashrate)
  - Acceptable: 90%+ linear scaling
  - Example: 2 GPUs @ 8 MH/s each = 16 MH/s expected, 14.4+ MH/s actual = pass
- ✅ Per-device hashrate within 5% of other devices (load balance)
- ✅ Callback latency per device < 500 µs
- ✅ Zero nonce collisions (verified via logging)

### Quality Requirements
- ✅ 100% test pass rate (unit + integration + stress)
- ✅ Zero CUDA errors in stable operation
- ✅ Zero memory leaks (verified via CUDA-MEMCHECK)
- ✅ 48+ hours stable mining on multi-GPU setup
- ✅ Thread-safe concurrent solution submissions
- ✅ Comprehensive error handling and logging

### Code Quality
- ✅ Zero compiler warnings
- ✅ RAII and smart pointer best practices
- ✅ Exception-safe callback handlers
- ✅ Const-correctness maintained
- ✅ Documented API changes
- ✅ Code review complete

---

## File Changes Summary

### Modified Files

**`include/ohmy/device_manager.hpp`**
- Add multi-device API: `initializeAllDevices()`, `getDeviceCount()`, `getDeviceInfo()`
- Add per-device hashrate: `getHashRate(deviceId)`, `getTotalHashRate()`
- Add per-device callbacks: `setResultCallback(deviceId)` or consolidated
- Add job context setter: `setMiningJobContext()` (broadcast to all)

**`src/cuda/device_manager.cu`**
- Refactor Impl class to manage device vector
- Per-device stream pipelines (vector<Streams>)
- Mining thread spawning and lifecycle
- Nonce range calculation and distribution
- Metrics aggregation per device
- Updated search() for multi-device iteration

**`src/cuda/result_callback.cu`**
- Add deviceId to ResultCallbackData
- Per-device logging in callback
- Atomic counter updates per device
- Thread-safe pool submission

**`src/network/stratum_client.hpp`**
- Add submitMutex_ for thread-safe submissions
- Document thread-safety guarantees

**`src/network/stratum_client.cpp`**
- Add lock_guard in submitSolution()
- Test concurrent submissions

**`src/miner_main.cpp`**
- Update device initialization: auto-detect all GPUs
- Update job context setting: broadcast to all threads
- Aggregate hashrate display from all devices
- Monitor thread health

**`tests/CMakeLists.txt`**
- Add multi-GPU tests to test suite
- Add stress test targets

### New Files

**`src/cuda/device_manager_multi_gpu.cu`** (optional, if separating logic)
- Multi-GPU specific kernel wrappers
- Per-device stream management

**`tests/test_multi_gpu.cpp`**
- Device enumeration tests
- Thread synchronization tests
- Nonce range collision detection
- Concurrent callback tests

**`tests/test_multi_gpu_pool.cpp`**
- Integration test with real pool
- Concurrent submission validation
- 60+ second mining test

---

## Performance Projections

**Single GPU (Phase 4 baseline):**
- GPU #0: 8.0 MH/s
- Total: 8.0 MH/s

**Dual GPU (Phase 5 projection):**
- GPU #0: 8.0 MH/s
- GPU #1: 8.0 MH/s
- Total: 16.0 MH/s (expected 14.4+ with acceptable overhead)

**Triple GPU (projected):**
- GPU #0: 8.0 MH/s
- GPU #1: 8.0 MH/s
- GPU #2: 8.0 MH/s
- Total: 24.0 MH/s (expected 21.6+ MH/s)

**Scaling Factor Target:** ≥ 90% linear

---

## Testing Plan

### Unit Tests
1. `test_device_enumeration` - Verify GPU detection
2. `test_device_initialization` - Verify per-device setup
3. `test_nonce_range_calculation` - Verify no collisions
4. `test_callback_thread_safety` - Verify concurrent callbacks
5. `test_hashrate_aggregation` - Verify accurate totals

### Integration Tests
1. `test_multi_gpu_mining` - 10 second mining on all GPUs
2. `test_multi_gpu_pool_submission` - Submit shares from all GPUs
3. `test_concurrent_job_update` - Update job while mining on all GPUs
4. `test_thread_shutdown` - Graceful shutdown of all mining threads

### Stress Tests
1. `stress_48_hour_mining` - 48 hours continuous mining on all GPUs
2. `stress_concurrent_callbacks` - High-frequency callbacks from multiple GPUs
3. `stress_pool_submission` - Rapid share submission from all GPUs
4. `stress_job_updates` - Frequent job updates during mining

### Performance Tests
1. `bench_multi_gpu_hashrate` - Measure scaling efficiency
2. `bench_callback_latency` - Per-device callback latency
3. `bench_nonce_exhaustion` - Time to exhaust nonce ranges
4. `bench_memory_usage` - Total VRAM consumption

---

## Risk Assessment & Mitigation

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|-----------|
| Nonce collision bugs | Data corruption | Low | Unit test collision detection, logging |
| Stratum mutex deadlock | Pool submission failure | Low | Code review, stress testing |
| CUDA context issues | Mining crashes | Medium | Per-thread validation, error tracking |
| Memory exhaustion (DAG × N) | OOM errors | Medium | Warn if insufficient VRAM, graceful degradation |
| Thread synchronization bugs | Race conditions | Medium | Thread sanitizer testing, careful design review |
| Hashrate calculation errors | Incorrect metrics | Low | Comparison with ethminer output |

---

## Rollback Plan

If Phase 5 encounters blocking issues:

1. **Fallback:** Revert to Phase 4 (single-GPU mode)
   - Feature branch feature/phase5-multi-gpu remains, does not merge
   - Trunk stays at Phase 4 (commit c98bb76)
   
2. **Partial Merge:** If device enumeration works but threading fails:
   - Merge device detection only
   - Leave threading for Phase 5.2 retry
   
3. **Known Workaround:** If StratumClient mutex causes deadlock:
   - Use lock-free queue for submissions
   - Or defer to Phase 6 optimization

---

## Timeline & Milestones

**Start Date:** November 9, 2025  
**Estimated Duration:** 2-3 weeks (15-21 days)

| Milestone | Target Date | Status |
|-----------|------------|--------|
| Architecture finalized | Nov 10 | 🚀 In Progress |
| Phase 5.1 (Device enumeration) | Nov 13 | ⏳ Planned |
| Phase 5.2 (Mining threads) | Nov 18 | ⏳ Planned |
| Phase 5.3 (Nonce distribution) | Nov 21 | ⏳ Planned |
| Phase 5.4 (Callbacks) | Nov 23 | ⏳ Planned |
| Phase 5.5 (Pool integration) | Nov 25 | ⏳ Planned |
| Phase 5.6 (Testing & validation) | Nov 30 | ⏳ Planned |
| Phase 5.7 (Documentation & merge) | Dec 1 | ⏳ Planned |

**Total Estimated Effort:** 120-160 engineering hours

---

## References & Resources

- NVIDIA CUDA Programming Guide: https://docs.nvidia.com/cuda/cuda-c-programming-guide/
- Multi-GPU CUDA Best Practices: https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/#multiple-gpus
- Thread-Safe Design: https://en.cppreference.com/w/cpp/atomic
- Ethash Specification: https://eth.wiki/concepts/ethash/ethash

---

**Document Created:** November 9, 2025  
**Author:** GitHub Copilot  
**Phase:** 5 (Multi-GPU Support)  
**Status:** 🚀 Active Planning
