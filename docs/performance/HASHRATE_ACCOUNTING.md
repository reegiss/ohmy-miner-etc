# Hashrate Accounting

Accurate hashrate measurement is critical. We use an event-driven, per-stream model.

## Goals
- Avoid inflated MH/s due to counting submitted (in-flight) work
- Provide stable averages over meaningful windows
- Keep overhead minimal (events are already needed for async pipeline control)

## Model
Each CUDA stream `S_i` launches kernel batches. For batch `b`:
1. Record start event `E_start_i`
2. Launch kernel scanning `N_b` nonces
3. Record stop event `E_stop_i`
4. When `E_stop_i` completes: add `N_b` to global completed hash count

We track a pending count per stream so only completed batches contribute.

## Formula
Let completed hashes over interval Δt be H_c.
Hashrate (MH/s) = (H_c / Δt) / 1e6
Where Δt is computed from earliest start event to latest completed stop event within the rolling window.

## Implementation Notes
- `pendingCounts_[i]` set when batch launched; cleared & applied when stop event signals
- Time measurement uses CUDA event elapsed time for precision
- Streams allow overlap; completion order may differ from launch order

## Edge Cases
- Long-running batch delays hashrate update (trade-off: responsiveness vs overhead)
- Failed kernel: detect via CUDA error check; do NOT add pending count
- Stream reset: pending cleared to prevent phantom hashes

## Recommendations
- Target batch kernel runtime 50–150 ms for responsive yet efficient accounting
- Use 2–4 streams initially; scale after profiling
- Always validate against pool-side effective hashrate over multi-minute periods
