#!/bin/bash
# Refined tuning around baseline (values 2,3,4,5,6,7)

BENCHMARK="/home/regis/develop/ohmy-miner-etc/build/tests/bench_cuda"

echo "═════════════════════════════════════════════════════════════"
echo "REFINED TUNING: Small values around baseline"
echo "═════════════════════════════════════════════════════════════"
echo ""

for NONCES in 1 2 3 4 5 6 7; do
    echo -n "NONCES=$NONCES: "
    OHMY_USE_OPTIMIZED_KERNEL=1 OHMY_NONCES_PER_THREAD=$NONCES timeout 30 $BENCHMARK 2>&1 | grep "Average Hashrate" | awk '{print $3, $4}' || echo "[ERROR]"
done
