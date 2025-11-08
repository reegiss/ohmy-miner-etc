# Stratum Protocol Guide

## Overview

Stratum is the most common protocol for pool mining. It uses JSON-RPC 2.0 over TCP/TLS.

## Connection Flow

```
1. Connect to pool (TCP socket)
2. Send mining.subscribe
3. Receive subscription details
4. Send mining.authorize
5. Receive authorization confirmation
6. Start receiving mining jobs
7. Submit solutions via mining.submit
```

## Message Format

All messages are JSON-RPC 2.0:

```json
{
  "id": 1,
  "method": "mining.subscribe",
  "params": []
}
```

## Methods

### mining.subscribe

Subscribe to mining notifications.

**Request:**
```json
{
  "id": 1,
  "method": "mining.subscribe",
  "params": ["ohmy-miner-etc/1.0.0"]
}
```

**Response:**
```json
{
  "id": 1,
  "result": [
    ["mining.notify", "subscription_id"],
    "extranonce1",
    4
  ],
  "error": null
}
```

### mining.authorize

Authorize worker with pool.

**Request:**
```json
{
  "id": 2,
  "method": "mining.authorize",
  "params": ["0xWalletAddress.worker", "password"]
}
```

**Response:**
```json
{
  "id": 2,
  "result": true,
  "error": null
}
```

### mining.notify

Pool sends new mining job (notification, not a response).

**Notification:**
```json
{
  "id": null,
  "method": "mining.notify",
  "params": [
    "job_id",
    "seed_hash",
    "header_hash",
    "clean_jobs"
  ]
}
```

### mining.set_difficulty

Pool adjusts difficulty.

**Notification:**
```json
{
  "id": null,
  "method": "mining.set_difficulty",
  "params": [4000000000]
}
```

### mining.submit

Submit found solution to pool.

**Request:**
```json
{
  "id": 4,
  "method": "mining.submit",
  "params": [
    "0xWalletAddress.worker",
    "job_id",
    "nonce_hex",
    "header_hash",
    "mix_hash"
  ]
}
```

**Response:**
```json
{
  "id": 4,
  "result": true,
  "error": null
}
```

## Error Handling

Common errors:
- **20**: Other/Unknown
- **21**: Job not found
- **22**: Duplicate share
- **23**: Low difficulty share
- **24**: Unauthorized worker
- **25**: Not subscribed

**Error Response:**
```json
{
  "id": 4,
  "result": null,
  "error": [21, "Job not found", null]
}
```

## Implementation Notes

### Connection Management

```cpp
// Keep-alive: send ping every 60 seconds
void keepAlive() {
    while (connected) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        send(R"({"id":99,"method":"mining.ping","params":[]})");
    }
}
```

### Thread Safety

Use separate threads for:
1. **Receive thread:** Handle incoming messages
2. **Send thread:** Send outgoing messages  
3. **Mining threads:** Process jobs and find solutions

### Reconnection

Implement exponential backoff:
```
delay = min(initial_delay * 2^attempts, max_delay)
```

## Popular ETC Pools

- 2Miners: `etc.2miners.com:1010`
- Ethermine: `etc.ethermine.org:4444`
- F2Pool: `etc.f2pool.com:8118`
- Nanopool: `etc-eu1.nanopool.org:19999`

## References

- [Stratum Protocol Documentation](https://en.bitcoin.it/wiki/Stratum_mining_protocol)
- [Stratum V2](https://stratumprotocol.org/)
