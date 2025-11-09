#!/bin/bash
# Start mining script with common configurations

POOL_URL="${POOL_URL:-stratum+tcp://etc.2miners.com:1010}"
WALLET="${WALLET:-0xe3c52bab8907c03b8305f9cd21d48a320de439b7}"
WORKER="${WORKER:-$(hostname)}"
DEVICE="${DEVICE:-0}"
VERBOSE="${VERBOSE:-0}"

if [ -z "$WALLET" ]; then
    echo "Error: WALLET environment variable not set"
    echo "Usage: WALLET=0xYourAddress ./scripts/start-mining.sh"
    exit 1
fi

echo "Starting OhMy Miner ETC"
echo "Pool: $POOL_URL"
echo "Wallet: $WALLET"
echo "Worker: $WORKER"
echo "Device: $DEVICE"

# Build verbose flag if set
VERBOSE_FLAG=""
if [ "$VERBOSE" = "1" ]; then
    VERBOSE_FLAG="--verbose"
fi

./build/src/ohmy-miner-etc \
    --pool "$POOL_URL" \
    --wallet "$WALLET" \
    --worker "$WORKER" \
    --device "$DEVICE" \
    $VERBOSE_FLAG
