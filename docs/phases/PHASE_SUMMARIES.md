# Phase Summaries

This document consolidates significant development phases and their rationale.

## 1) GPU-first zero-copy DAG (RAM reduction)
- Problem: Host RAM ballooned (~4 GB) due to DAG moving GPU→CPU→GPU
- Change: Generate DAG on GPU and keep it in VRAM; pass device pointer into mining kernels
- Result: Host RAM dropped to ~100–150 MB during mining; improved startup and stability

## 2) Accurate hashrate via event-driven accounting
- Problem: Hashrate inflated by counting submitted work before completion
- Change: Track per-stream in-flight counts, increment totals only on CUDA event completion; measure elapsed time from per-stream events
- Result: Stable, realistic MH/s over multi-minute windows

## 3) Logging cleanup
- Problem: Spammy recurring debug logs (e.g., job context set) obscured important info
- Change: Removed/guarded noisy logs; standardized log levels
- Result: Cleaner runtime output and easier debugging

## 4) Documentation consolidation
- Problem: Scattered root-level Markdown made onboarding harder
- Change: Introduced `docs/INDEX.md` as the canonical entry point; categorized performance and phase docs
- Result: Faster navigation and clearer project story

## Next candidates
- Unit tests for event-based accounting across multi-stream scenarios
- Performance tuning playbooks per GPU architecture (Pascal, Turing, Ampere, Ada)
- Stratum reconnection/backoff hardening and integration tests
