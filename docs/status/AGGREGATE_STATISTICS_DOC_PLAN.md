# Aggregate Statistics Documentation Plan

Goal: unify scattered per-phase performance/statistics outputs into a single authoritative doc.

## Sources
- Phase summaries (PHASE3..PHASE6 files)
- Multi-GPU implementation report (PHASE5_IMPLEMENTATION_REPORT.md)
- Logging fixes (PHASE6_LOGGING_FIXES.md)
- Nonces tuning raw reports (NONCES_OPTIMIZATION_README.md, NONCES_TUNING_EXECUTIVE_SUMMARY.md)

## Target Docs
- `docs/performance/NONCES_TUNING.md` (tuning + raw improvement metrics)
- `docs/performance/HASHRATE_ACCOUNTING.md` (methodology + formula)
- `docs/phases/PHASE_SUMMARIES.md` (narrative timeline)
- NEW (planned): `docs/status/PERFORMANCE_OVERVIEW.md` (aggregate numeric dashboard)

## PERFORMANCE_OVERVIEW.md Structure (Planned)
1. Executive Summary
2. Hashrate Evolution Table
3. Memory Footprint Evolution (Host vs Device)
4. Stream Architecture Progression
5. Multi-GPU Scaling Expectations
6. Tuning Parameters (current defaults + override envs)
7. Pending Optimization Opportunities

## Data Points To Extract
- Baseline single-GPU hashrate per phase
- Occupancy / register pressure notes (from nonces tuning)
- Host RAM before/after zero-copy DAG
- Stream count dynamic tuning outcomes
- Callback latency ranges (Phase 4)
- Multi-GPU linear scaling assumption notes

## Next Actions
1. Build hash rate evolution table (Phase 2 → 6 + tuning improvements)
2. Verify memory usage numbers in zero-copy DAG phase file
3. Extract register pressure metrics into concise bullet list
4. Create PERFORMANCE_OVERVIEW.md with sections above
5. Link from `docs/INDEX.md` under Performance

## Status
Planned; not yet implemented. This file tracks consolidation tasks before removal of legacy phase documents.
