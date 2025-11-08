#!/bin/bash

# Test script for OhMy Miner ETC with 2miners pool
# Pool: etc.2miners.com:1010
# Wallet: 0xe3c52bab8907c03b8305f9cd21d48a320de439b7

POOL="etc.2miners.com:1010"
WALLET="0xe3c52bab8907c03b8305f9cd21d48a320de439b7"
WORKER="test-rig-$(hostname)"

echo "═══════════════════════════════════════════════════════════"
echo "  OhMy Miner ETC - Test Script"
echo "═══════════════════════════════════════════════════════════"
echo "Pool: $POOL"
echo "Wallet: $WALLET"
echo "Worker: $WORKER"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "Starting miner..."
echo "Press Ctrl+C to stop"
echo ""

cd "$(dirname "$0")/build/src"
./ohmy-miner-etc --pool "$POOL" --wallet "$WALLET" --worker "$WORKER" --verbose
