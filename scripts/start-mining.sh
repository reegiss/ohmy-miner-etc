#!/bin/bash
# Start mining script with common configurations

POOL_URL="${POOL_URL:-stratum+tcp://etc.2miners.com:1010}"
WALLET="${WALLET:-}"
WORKER="${WORKER:-$(hostname)}"
DEVICES="${DEVICES:-0}"
LOG_FILE="${LOG_FILE:-ohmy-miner.log}"

if [ -z "$WALLET" ]; then
    echo "Error: WALLET environment variable not set"
    echo "Usage: WALLET=0xYourAddress ./scripts/start-mining.sh"
    exit 1
fi

echo "Starting OhMy Miner ETC"
echo "Pool: $POOL_URL"
echo "Wallet: $WALLET"
echo "Worker: $WORKER"
echo "Devices: $DEVICES"
echo "Log: $LOG_FILE"

./build/ohmy-miner-etc \
    --pool "$POOL_URL" \
    --wallet "$WALLET" \
    --worker "$WORKER" \
    --devices "$DEVICES" \
    --log "$LOG_FILE"
