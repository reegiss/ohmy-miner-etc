#!/bin/bash
# Quick test for a specific NONCES_PER_THREAD value

set -e

NONCES=${1:-32}
TIMEOUT=${2:-45}

echo "Testing NONCES_PER_THREAD=$NONCES (timeout $TIMEOUT seconds)..."

# Build
cd /home/regis/develop/ohmy-miner-etc
if [ ! -d build ]; then
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    cd ..
else
    cd build
    make -j$(nproc)
    cd ..
fi

# Run benchmark
export OHMY_USE_OPTIMIZED_KERNEL=1
export OHMY_NONCES_PER_THREAD=$NONCES

timeout ${TIMEOUT} ./build/tests/bench_cuda 2>&1 | grep -E "Hash Rate|Average|Hashrate|nonces" || true
