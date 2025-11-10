## Documentation Index

This index consolidates and organizes the project documentation previously scattered across many Markdown files in the repository root. Core historical, performance, and build artifacts have been synthesized into a smaller, navigable structure.

### 1. Getting Started
- `README.md` – High-level overview & quick start
- `CONTRIBUTING.md` – Contribution workflow & style
- `LICENSE` – MIT license

### 2. Architecture & Core Concepts
- `docs/ARCHITECTURE.md` – System design & module boundaries
- `docs/ETHASH.md` – Ethash / Etchash algorithm details
- `docs/STRATUM.md` – Stratum protocol behavior & message flow
- `docs/CUDA_OPTIMIZATION.md` – CUDA performance guidelines
- `docs/QUICK_REFERENCE.md` – Commands & common developer actions

### 3. Phase / Milestone History
Synthesized summaries (instead of many root phase files):
- `docs/phases/PHASE_SUMMARIES.md` – Consolidated Phase 4, 5, 6 implementation & outcomes

Legacy detailed reports (original, verbose) still present in repo root for now; can be moved to `docs/archive/` in a follow‑up cleanup if desired:
```
PHASE4_COMPLETION_SUMMARY.md
PHASE4_FINAL_STATUS.md
PHASE5_COMPLETE.md
PHASE5_IMPLEMENTATION_REPORT.md
PHASE6_ASYNC_PIPELINE_IMPLEMENTATION.md
PHASE6_LOGGING_FIXES.md
PHASE6_VALIDATION_PLAN.md
SESSION_SUMMARY.md
SESSION_SUMMARY_PHASE6.md
```

### 4. Performance & Tuning
- `docs/performance/NONCES_TUNING.md` – Consolidated NONCES_PER_THREAD empirical tuning (merges raw & executive summaries)
- `docs/performance/HASHRATE_ACCOUNTING.md` – Event-driven per-stream logic & formula
- `docs/status/PERFORMANCE_OVERVIEW.md` – Aggregated evolution (hashrate, memory, pipeline)

Raw benchmark artifact: `benchmark_results.json` (machine‑generated)

### 5. Build & Test Infrastructure
- `docs/build/BUILD_RESULTS.md` – Canonical build/test snapshot (replaces root `BUILD_TEST_RESULTS.md`)
- CMake configuration: `CMakeLists.txt` (root + subdirectories)
- Scripts: `scripts/*.sh`

### 6. Project Planning & Status
- `ROADMAP.md` – Long-term phased roadmap
- `PROJECT_STATUS.md` – (Legacy high-level status; can be merged into ROADMAP in future)
- `COMMIT_PLAN.md` – (Legacy Phase 5 commit sequencing; retained for historical traceability)

### 7. Mining Runtime / Operations
- `scripts/start-mining.sh` – Standard launch wrapper
- Environment overrides:
  - `OHMY_NUM_STREAMS` – Force async pipeline stream count
  - `OHMY_NONCES_PER_THREAD` – Override tuned NONCES_PER_THREAD (default optimized = 1)
  - `OHMY_USE_OPTIMIZED_KERNEL` – Select optimized kernel variant

### 8. GPU Pipeline & Optimization Highlights
Key architectural progression (see `docs/phases/PHASE_SUMMARIES.md` for details):
1. Phase 4 – Async host callbacks remove GPU blocking
2. Phase 5 – Multi-GPU thread-safe scaling & per-device statistics
3. Phase 6 – N-stream asynchronous pipeline (triple buffering + dynamic stream tuning)
4. Post Phase 6 – Accurate hashrate via per-stream completion accounting

### 9. Suggested Future Consolidations
To reduce root clutter further, consider moving the following into `docs/archive/` or merging into existing synthesized documents:
- `SETUP_COMPLETE.md` (content overlaps README + ARCHITECTURE)
- `SUMMARY.md` (redundant with README + PHASE summaries)
- `PROJECT_STATUS.md` (merge key deltas into ROADMAP)

### 10. How to Extend Documentation
When adding new substantial features:
1. If it is a development phase milestone → append a concise section to `docs/phases/PHASE_SUMMARIES.md`.
2. If it is a deep performance study → add `docs/performance/<topic>.md` and link here.
3. If it changes public usage → update `README.md` + QUICK_REFERENCE.
4. If it modifies pipeline architecture → update `ARCHITECTURE.md` and relevant phase summary.

---
Generated consolidation (2025-11-09). This index is the canonical entry point for project documentation going forward.
# Documentation Index

A curated index of the project documentation, grouped by topic.

## Getting Started
- [README](../README.md) — Top-level overview, usage, and setup
- [docs/README](README.md) — Docs area overview
- [CONTRIBUTING](../CONTRIBUTING.md) — How to contribute
- [CHANGELOG](../CHANGELOG.md) — Notable changes

## Architecture & Design
- [Architecture](ARCHITECTURE.md) — High-level architecture
- [ETHASH](ETHASH.md) — Algorithm details and our GPU-first approach
- [STRATUM](STRATUM.md) — Pool protocol integration
- [QUICK_REFERENCE](QUICK_REFERENCE.md) — Key concepts and commands

## Core Modules
- Pipeline & Async
  - [ASYNC_MULTISTREAM_OPTIMIZATION](ASYNC_MULTISTREAM_OPTIMIZATION.md)
  - [PHASE2_ASYNC_MEMCPY_REPORT](PHASE2_ASYNC_MEMCPY_REPORT.md)
  - [PHASE3_3STREAM_IMPLEMENTATION_REPORT](PHASE3_3STREAM_IMPLEMENTATION_REPORT.md)
  - [PHASE4_ASYNC_CALLBACKS_PLANNING](PHASE4_ASYNC_CALLBACKS_PLANNING.md)
  - [PHASE4_ENGINE_INTEGRATION_REPORT](PHASE4_ENGINE_INTEGRATION_REPORT.md)
  - [PHASE6_ASYNC_PIPELINE_IMPLEMENTATION](../PHASE6_ASYNC_PIPELINE_IMPLEMENTATION.md)
  - [SESSION_ASYNC_MULTISTREAM](SESSION_ASYNC_MULTISTREAM.md)
- GPU Kernels
  - [WARP_KERNEL_ANALYSIS](WARP_KERNEL_ANALYSIS.md)
  - [TEXTURE_MEMORY_RESULTS](TEXTURE_MEMORY_RESULTS.md)
  - [PERFORMANCE_OPTIMIZATION_RESULTS](PERFORMANCE_OPTIMIZATION_RESULTS.md)
  - [KERNEL_LAUNCHER](KERNEL_LAUNCHER.md)

## Refactoring & Phases
- [PHASE3_PLANNING](PHASE3_PLANNING.md)
- [PHASE4_ASYNC_CALLBACKS_PLANNING](PHASE4_ASYNC_CALLBACKS_PLANNING.md)
- [PHASE4_ENGINE_INTEGRATION_REPORT](PHASE4_ENGINE_INTEGRATION_REPORT.md)
- [PHASE4_COMPLETION_SUMMARY](../PHASE4_COMPLETION_SUMMARY.md)
- [PHASE4_FINAL_STATUS](../PHASE4_FINAL_STATUS.md)
- [PHASE5_MULTI_GPU_PLANNING](PHASE5_MULTI_GPU_PLANNING.md)
- [PHASE5_PROGRESS_REPORT](PHASE5_PROGRESS_REPORT.md)
- [PHASE5_IMPLEMENTATION_REPORT](../PHASE5_IMPLEMENTATION_REPORT.md)
- [PHASE5_COMPLETE](../PHASE5_COMPLETE.md)
- [PHASE6_LOGGING_FIXES](../PHASE6_LOGGING_FIXES.md)
- [PHASE6_VALIDATION_PLAN](../PHASE6_VALIDATION_PLAN.md)

## Tuning & Benchmarks
- [NONCES_OPTIMIZATION_README](../NONCES_OPTIMIZATION_README.md)
- [NONCES_TUNING_EXECUTIVE_SUMMARY](../NONCES_TUNING_EXECUTIVE_SUMMARY.md)
- [SESSION_NONCES_TUNING](../SESSION_NONCES_TUNING.md)
- [benchmark_results.json](../benchmark_results.json)

## Status & Summaries
- [ROADMAP](../ROADMAP.md)
- [PROJECT_STATUS](../PROJECT_STATUS.md)
- [BUILD_TEST_RESULTS](../BUILD_TEST_RESULTS.md)
- Session summaries:
  - [SESSION_SUMMARY](../SESSION_SUMMARY.md)
  - [SESSION_SUMMARY_PHASE6](../SESSION_SUMMARY_PHASE6.md)
  - [SESSION_CLEANUP_AND_TESTING](SESSION_CLEANUP_AND_TESTING.md)
  - [SESSION_SUMMARY_WARP_INVESTIGATION](SESSION_SUMMARY_WARP_INVESTIGATION.md)

## Help & Guides
- [help/FAQ](../help/FAQ.md)
- [help/API](../help/API.md)
- [help/WebUI](../help/WebUI.md)
- [help/Dual mining](../help/Dual mining.md)
- [help/LHR](../help/LHR.md)

## Misc
- [SUMMARY](../SUMMARY.md)
- [SETUP_COMPLETE](../SETUP_COMPLETE.md)
- [.github/copilot-instructions](../.github/copilot-instructions.md)
