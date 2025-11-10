# Nonces, Throughput, and Tuning

This guide explains how to tune kernel launch parameters and pipeline concurrency for the best hashrate while preserving correctness and memory efficiency.

## Concepts
- Nonces: Each kernel launch scans a nonce window; the count per batch drives throughput
- Streams: Multiple CUDA streams overlap compute and transfers
- Events: Start/stop events per stream mark completion for accurate accounting

## What to tune
- Grid/Block size
  - Choose blockDim.x as a multiple of warp size (32) and suited to SM resources
  - gridDim should fully occupy SMs; avoid extreme over-subscription that increases latency
- Batches per tick
  - Size nonce windows to balance kernel duration (e.g., 50–150 ms) for smooth telemetry and responsiveness
- Stream count (N)
  - Start with 2–4 and profile; increase until incremental gains flatten

## Methodology
1. Pick a baseline block/grid based on target GPU architecture
2. Run for 2–5 minutes to stabilize thermals and clocks
3. Use event-driven accounting; only completed batches contribute to hashrate
4. Profile with Nsight Compute to check occupancy, stalls, and memory bandwidth

## Pitfalls
- Counting submitted work instead of completed work inflates MH/s
- Host RAM explosions if DAG is copied GPU→CPU→GPU; keep DAG on device
- Overly small batches cause excessive launch overhead; overly large batches delay result reporting

## Metrics to track
- MH/s averaged over last N seconds (e.g., 30–60)
- Kernel execution time per stream (ms)
- GPU utilization and memory bandwidth
- Rejected/accepted shares and latency to pool

## Checklist
- [ ] DAG generated and resident on GPU
- [ ] Streams configured; events recorded around every kernel launch
- [ ] Pending counts tracked per stream and cleared only on completion
- [ ] Hashed nonces accounted strictly after event completion

## Proven baseline (from recent tuning)
- Optimal NONCES_PER_THREAD observed: 1 (vs previous default 4)
- Measured gain: ~+2.7% (8.02 MH/s vs 7.81 MH/s on GTX 1660 SUPER)
- Rationale: lower register pressure → higher occupancy, zero local memory spill
- Validation: benchmarks, unit tests, short pool runs

Environment override
```
export OHMY_NONCES_PER_THREAD=1
export OHMY_USE_OPTIMIZED_KERNEL=1
```

Note: Always validate on your specific GPU architecture; the above baseline was measured on Turing (sm_75).
