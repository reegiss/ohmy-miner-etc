#!/bin/bash
# Fast sequential test of NONCES_PER_THREAD values

echo "Testing NONCES_PER_THREAD tuning..."
echo ""

TEST_VALUES="32 64 128 256"
RESULTS_FILE="/tmp/nonces_results.txt"

> $RESULTS_FILE

for NONCES in $TEST_VALUES; do
    echo "═══════════════════════════════════"
    echo "Testing NONCES_PER_THREAD=$NONCES"
    echo "═══════════════════════════════════"
    
    # Clean and rebuild
    cd /home/regis/develop/ohmy-miner-etc
    rm -rf build 2>/dev/null
    mkdir -p build
    
    cd build
    export OHMY_NONCES_PER_THREAD=$NONCES
    cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    make -j$(nproc) >/dev/null 2>&1
    
    if [ $? -ne 0 ]; then
        echo "[ERROR] Compilation failed for NONCES=$NONCES"
        echo "$NONCES: FAILED" >> $RESULTS_FILE
        continue
    fi
    
    # Run benchmark
    export OHMY_USE_OPTIMIZED_KERNEL=1
    OUTPUT=$(timeout 30 ./tests/bench_cuda 2>&1)
    HASHRATE=$(echo "$OUTPUT" | grep "Average Hashrate" | awk '{print $3}')
    
    if [ -z "$HASHRATE" ]; then
        echo "[ERROR] No hashrate extracted for NONCES=$NONCES"
        echo "$NONCES: NO DATA" >> $RESULTS_FILE
    else
        echo "[OK] Hashrate: $HASHRATE MH/s"
        echo "$NONCES: $HASHRATE" >> $RESULTS_FILE
    fi
    
    echo ""
done

echo ""
echo "═══════════════════════════════════"
echo "SUMMARY"
echo "═══════════════════════════════════"
cat $RESULTS_FILE
