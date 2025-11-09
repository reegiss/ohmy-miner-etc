#!/bin/bash
# Quick test - no recompilation needed (NONCES_PER_THREAD is read at runtime)

BENCHMARK="/home/regis/develop/ohmy-miner-etc/build/tests/bench_cuda"

echo "═════════════════════════════════════════════════════════════"
echo "NONCES_PER_THREAD Tuning (Runtime Configuration)"
echo "═════════════════════════════════════════════════════════════"
echo ""
echo "Baseline (default=4): "
OHMY_USE_OPTIMIZED_KERNEL=1 timeout 30 $BENCHMARK 2>&1 | grep "Average Hashrate"
echo ""

for NONCES in 8 16 32 64 128 256; do
    echo "Testing NONCES=$NONCES: "
    OHMY_USE_OPTIMIZED_KERNEL=1 OHMY_NONCES_PER_THREAD=$NONCES timeout 30 $BENCHMARK 2>&1 | grep "Average Hashrate" || echo "  [ERROR]"
    echo ""
done
