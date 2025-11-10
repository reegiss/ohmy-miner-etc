# Performance Overview

This page aggregates key performance metrics and evolution across phases.

## Hashrate Evolution (single GPU example)
- Phase 2 baseline: ~7.80 MH/s
- Phase 2.5 (NONCES=1): ~8.02 MH/s (+2.7%)
- Phase 3 (3-stream): ~8.02 MH/s (stable)
- Phase 4 (async callbacks): ~8.00 MH/s (GPU unblocked)
- Phase 6 (async multi-stream): target 9–10+ MH/s (post-validation)

## Memory Footprint
- Pre zero-copy DAG: Host RAM ~4.1 GB (due to DAG GPU→CPU→GPU)
- Zero-copy DAG: Host RAM ~100–150 MB during mining

## Pipeline Architecture
- Phase 3: 3-stream event chain
- Phase 4: Non-blocking pool submission via callbacks
- Phase 6: N-stream async pipeline with per-stream events

## Tuning Defaults
- NONCES_PER_THREAD: 1 (override: OHMY_NONCES_PER_THREAD)
- Streams: dynamic (override: OHMY_NUM_STREAMS)

## Validation Notes
- Hashrate must count completed work only (see HASHRATE_ACCOUNTING.md)
- Compare against pool effective hashrate over multi-minute windows

## Links
- docs/performance/NONCES_TUNING.md
- docs/performance/HASHRATE_ACCOUNTING.md
- docs/phases/PHASE_SUMMARIES.md