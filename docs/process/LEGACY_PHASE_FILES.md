# Legacy Phase Files (Root)

The following root-level phase/session markdown files have been consolidated. Their core content is (or will be) represented inside `docs/phases/PHASE_SUMMARIES.md` and related performance docs.

## To Consolidate / Mark Legacy
- PHASE3_COMPLETION_SUMMARY.txt
- PHASE4_COMPLETION_SUMMARY.md
- PHASE4_FINAL_STATUS.md
- PHASE5_COMPLETE.md
- PHASE5_IMPLEMENTATION_REPORT.md
- PHASE6_ASYNC_PIPELINE_IMPLEMENTATION.md
- PHASE6_LOGGING_FIXES.md
- PHASE6_VALIDATION_PLAN.md
- SESSION_SUMMARY.md
- SESSION_SUMMARY_PHASE6.md
- SUMMARY.md
- SESSION_NONCES_TUNING.md
- NONCES_OPTIMIZATION_README.md
- NONCES_TUNING_EXECUTIVE_SUMMARY.md
- TUNING_SUMMARY.txt

## Action Plan
1. Extract distilled bullet highlights into `docs/phases/PHASE_SUMMARIES.md`
2. Merge all nonces tuning data into `docs/performance/NONCES_TUNING.md` (already partially done)
3. Add hashrate/accounting improvements into `docs/performance/HASHRATE_ACCOUNTING.md` (done)
4. Create a single "Async Pipeline" condensed section in `PHASE_SUMMARIES.md` (pending)
5. After verification, delete or move legacy files to `docs/legacy/` (optional step)

## Deletion Candidates (After Verification)
- SUMMARY.md (duplicate of session summaries)
- SESSION_SUMMARY_PHASE6.md (content merged)
- SESSION_SUMMARY.md (content merged)
- TUNING_SUMMARY.txt (data merged)

## Keep (For Now)
- PHASE5_IMPLEMENTATION_REPORT.md (reference until multi-GPU doc consolidated)
- PHASE6_ASYNC_PIPELINE_IMPLEMENTATION.md (reference until async pipeline condensed)
- NONCES_OPTIMIZATION_README.md (retain raw data for audit until final merge complete)

## Notes
- Do not remove until cross-checked that every key metric (hashrate, register pressure notes, timing, stream counts) is in consolidated docs.
- Add a CHANGELOG entry referencing consolidation once finalized.
