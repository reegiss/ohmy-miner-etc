#!/bin/bash
# Benchmark script for ohmy-miner-etc kernel comparison
# Usage: ./scripts/benchmark_kernels.sh [duration_seconds]

set -e

DURATION=${1:-20}
POOL="etc.2miners.com:1010"
WALLET="0x742d35Cc6731C0532925a3b8D94d30A8f33b1234"
BUILD_DIR="./build"
MINER="$BUILD_DIR/src/ohmy-miner-etc"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                                                           ║${NC}"
echo -e "${GREEN}║         OhMy Miner ETC - Kernel Benchmark Tool            ║${NC}"
echo -e "${GREEN}║                                                           ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if miner binary exists
if [ ! -f "$MINER" ]; then
    echo -e "${RED}Error: Miner binary not found at $MINER${NC}"
    echo "Please build the project first: cd build && make -j\$(nproc)"
    exit 1
fi

echo -e "${YELLOW}Duration per test: ${DURATION}s${NC}"
echo ""

# Function to extract hashrate from log
extract_hashrate() {
    local log_file=$1
    grep "Hash Rate:" "$log_file" | head -1 | awk '{print $5}'
}

# Test BASE kernel
echo -e "${GREEN}[1/2] Testing BASE kernel (1 nonce/thread)...${NC}"
LOGFILE_BASE="/tmp/bench_base_$$.log"
timeout "${DURATION}s" "$MINER" --pool "$POOL" --wallet "$WALLET" --worker bench-base > "$LOGFILE_BASE" 2>&1 || true

HASHRATE_BASE=$(extract_hashrate "$LOGFILE_BASE")
echo -e "  → Hash Rate: ${GREEN}${HASHRATE_BASE} MH/s${NC}"
echo ""

# Test OPTIMIZED kernel
echo -e "${GREEN}[2/2] Testing OPTIMIZED kernel (4 nonces/thread + shared memory)...${NC}"
LOGFILE_OPT="/tmp/bench_opt_$$.log"
OHMY_USE_OPTIMIZED_KERNEL=1 timeout "${DURATION}s" "$MINER" --pool "$POOL" --wallet "$WALLET" --worker bench-opt > "$LOGFILE_OPT" 2>&1 || true

HASHRATE_OPT=$(extract_hashrate "$LOGFILE_OPT")
echo -e "  → Hash Rate: ${GREEN}${HASHRATE_OPT} MH/s${NC}"
echo ""

# Calculate improvement
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}RESULTS SUMMARY${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

if [ -n "$HASHRATE_BASE" ] && [ -n "$HASHRATE_OPT" ]; then
    python3 << EOF
base = float("${HASHRATE_BASE}")
opt = float("${HASHRATE_OPT}")
gain = ((opt - base) / base) * 100

print(f"  Base Kernel:       {base:.2f} MH/s")
print(f"  Optimized Kernel:  {opt:.2f} MH/s")
print(f"  ")
if gain > 0:
    print(f"  Performance Gain:  \033[1;32m+{gain:.2f}%\033[0m 🚀")
else:
    print(f"  Performance Change: \033[0;31m{gain:.2f}%\033[0m")
EOF
else
    echo -e "${RED}  Error: Could not extract hashrate from logs${NC}"
    echo "  Log files saved for inspection:"
    echo "    - Base: $LOGFILE_BASE"
    echo "    - Optimized: $LOGFILE_OPT"
fi

echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Cleanup
rm -f "$LOGFILE_BASE" "$LOGFILE_OPT"

echo -e "${GREEN}Benchmark complete!${NC}"
