# Mock Token Dispenser Design

**Date:** 2026-02-16
**Purpose:** Local development tool for testing client without physical hardware
**Language:** Go
**Location:** `dispenser-mock/` (new top-level directory)

---

## Overview

The mock dispenser is a standalone Go HTTP server that implements the complete dispenser protocol (v1.1.0) for local development. It enables testing the TUI client and other consumers without requiring the ESP8266 hardware or Azkoyen Hopper.

### Use Case

Primary use case is **local development** - running the `dispenser-client-tui` without physical hardware to test UI flows, error handling, and protocol edge cases.

---

## Architecture

### Core Components

- **Single Go binary** (`dispenser-mock`)
- **HTTP server** using standard library `net/http`
- **In-memory state machine** matching ESP8266 behavior
- **Quantity-based scenario selection** (1-20 maps to different behaviors)

### How It Works

1. Start mock: `dispenser-mock --api-key=dev --bind=:8080`
2. Mock implements all three protocol endpoints:
   - `GET /health` (public)
   - `POST /dispense` (auth required)
   - `GET /dispense/{tx_id}` (auth required)
3. Client sends dispense request with quantity
4. Mock determines scenario from quantity field
5. Simulates behavior (success, timeout, crash, hardware error, etc.)
6. State persists in memory during mock lifetime, fresh start on restart

### Key Features

- **Realistic timing:** ~100ms per token (matches real hardware)
- **Progressive updates:** `dispensed` count increments in real-time
- **Socket-level failures:** Closed connections, timeouts
- **Protocol-level failures:** Crash recovery states, hardware errors
- **Full idempotency:** Transaction history ring buffer
- **Concurrent handling:** 409 conflicts for busy state
- **No persistence:** Clean slate on each startup

---

## Scenario Catalog

The mock uses the `quantity` field in the dispense request to determine test behavior.

### Success Scenarios

| Quantity | Behavior |
|----------|----------|
| 1-3      | Normal successful dispense (100ms/token) |
| 16-20    | Normal successful dispense (higher quantities) |

### Failure Scenarios

| Quantity | Behavior | Description |
|----------|----------|-------------|
| 4        | Timeout/jam | Dispenses 2 tokens, then 5s timeout triggers error state |
| 5        | Crash | Dispenses 1 token, then closes socket (simulates power loss) |
| 6        | Partial dispense | Dispenses 4 of 6 tokens, then jam |
| 7        | Load delay | First token takes 2.5s (empty hopper load time), remaining at 100ms |

### Hardware Error Scenarios

Simulates Azkoyen Hopper error codes (reported in `GET /health` response):

| Quantity | Error Code | Type | Description |
|----------|------------|------|-------------|
| 8        | 1 | COIN_STUCK | Coin jammed at exit sensor |
| 9        | 2 | SENSOR_OFF | Exit sensor stuck off |
| 10       | 3 | JAM_PERMANENT | Permanent jam detected |
| 11       | 4 | MAX_SPAN | Motor running too long |
| 12       | 5 | MOTOR_FAULT | Motor won't start |
| 13       | 6 | SENSOR_FAULT | Sensor disconnected |
| 14       | 7 | POWER_FAULT | Power supply abnormal |

### Special Test Scenarios

| Quantity | Behavior | Description |
|----------|----------|-------------|
| 15       | Slow dispense | 500ms/token (tests client polling patience) |

---

## Implementation Details

### Project Structure

```
dispenser-mock/
├── main.go           # CLI parsing, server setup, --list-scenarios
├── handler.go        # HTTP handlers (health, dispense, status endpoints)
├── state.go          # State machine, transaction tracking, scenario execution
├── types.go          # Protocol types (HealthResponse, DispenseRequest, etc.)
└── go.mod
```

### State Management

**MockDispenser struct:**
- Current state: `idle`, `dispensing`, `done`, `error`
- Active transaction: `tx_id`, `quantity`, `dispensed` count
- Transaction history: ring buffer (last 8 transactions for idempotency)
- Metrics: `total_dispenses`, `successful`, `jams`, `partial`, `failures`
- Hardware error state: `error.active`, `error.code`, `error.type`
- Thread-safe: `sync.RWMutex` for concurrent request handling

### Scenario Execution Flow

When a `POST /dispense` request arrives:

1. **Validate request:** Check API key, parse JSON, validate `tx_id` and `quantity`
2. **Check idempotency:** If `tx_id` exists in history, return cached state
3. **Determine scenario:** Map `quantity` to scenario behavior
4. **Execute scenario:**
   - **Success:** Spawn goroutine that increments `dispensed` every 100ms until complete
   - **Timeout:** Spawn goroutine that stops at partial count and enters error state
   - **Crash:** Start dispensing, then close socket after first token
   - **Hardware error:** Set error state with appropriate code/type
   - **Load delay:** Sleep 2.5s before first token, then 100ms per token
5. **Return response:** `{"tx_id": "...", "state": "dispensing", "quantity": N, "dispensed": 0}`

### Timing Implementation

Use `time.Ticker` for progressive dispense simulation:

```go
ticker := time.NewTicker(100 * time.Millisecond)
defer ticker.Stop()

for range ticker.C {
    dispensed++
    if dispensed >= quantity {
        state = "done"
        break
    }
}
```

### Type Reuse

Import types from existing client code to ensure compatibility:

```go
import (
    "remote-token-dispenser/dispenser-client-tui"
)

type HealthResponse = tui.HealthResponse
type DispenseRequest = tui.DispenseRequest
// etc.
```

---

## CLI & Configuration

### Command-Line Flags

```bash
dispenser-mock [flags]

Flags:
  --bind string       Network address to bind to (default ":8080")
  --api-key string    Required API key for auth (default "dev")
  --list-scenarios    Print scenario mapping table and exit
  --help              Show usage information
```

### Default Configuration

- **Bind address:** `:8080` (all interfaces, port 8080)
- **API key:** `dev`
- **Rationale:** Dev-friendly defaults (no sudo required), clear distinction from real hardware (port 80)

### Usage Examples

**Start with defaults:**
```bash
$ dispenser-mock
Mock dispenser listening on :8080
API Key: dev
Ready for requests. Use --list-scenarios to see available test cases.
```

**Custom configuration:**
```bash
$ dispenser-mock --bind=:9090 --api-key=secret123
Mock dispenser listening on :9090
API Key: secret123
```

**List available scenarios:**
```bash
$ dispenser-mock --list-scenarios
Token Dispenser Mock - Scenario Mapping
========================================

Request quantity determines test scenario:

SUCCESS:
  1-3     Normal dispense (100ms/token)
  16-20   Normal dispense (higher quantities)

FAILURES:
  4       Timeout after 2 tokens (5s jam detection)
  5       Crash after 1 token (socket close)
  6       Partial dispense (4 of 6 tokens)
  7       Load delay (2.5s first token, then 100ms)

HARDWARE ERRORS:
  8       COIN_STUCK (error code 1)
  9       SENSOR_OFF (error code 2)
  10      JAM_PERMANENT (error code 3)
  11      MAX_SPAN (error code 4)
  12      MOTOR_FAULT (error code 5)
  13      SENSOR_FAULT (error code 6)
  14      POWER_FAULT (error code 7)

SPECIAL:
  15      Slow dispense (500ms/token)

Example: curl -X POST http://localhost:8080/dispense \
  -H "X-API-Key: dev" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test1","quantity":4}'
```

---

## Testing the Mock

### With TUI Client

Modify TUI client config to point to mock:

```bash
# In dispenser-client-tui/
$ go run . --url=http://localhost:8080 --api-key=dev
```

### With curl

**Health check (no auth):**
```bash
$ curl http://localhost:8080/health | jq
```

**Normal dispense (1 token):**
```bash
$ curl -X POST http://localhost:8080/dispense \
  -H "X-API-Key: dev" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test1","quantity":1}'
```

**Timeout scenario (4 tokens):**
```bash
$ curl -X POST http://localhost:8080/dispense \
  -H "X-API-Key: dev" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test2","quantity":4}'

# Poll status
$ curl -H "X-API-Key: dev" http://localhost:8080/dispense/test2
```

**Crash scenario (5 tokens):**
```bash
$ curl -X POST http://localhost:8080/dispense \
  -H "X-API-Key: dev" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test3","quantity":5}'
# Connection will close after ~100ms
```

---

## Implementation Notes

### Dependencies

- **Standard library only:** `net/http`, `encoding/json`, `sync`, `time`
- **No external packages** (except for testing)

### Thread Safety

- All state mutations protected by `sync.RWMutex`
- Goroutines for dispense simulation coordinate via channels
- Safe for concurrent requests

### Error Handling

- Invalid API key → 401 Unauthorized
- Invalid JSON → 400 Bad Request
- Missing Content-Type → 415 Unsupported Media Type
- Dispenser busy → 409 Conflict
- Unknown tx_id → 404 Not Found

### Logging

Log all requests with timestamp, method, path, and outcome:

```
2026-02-16 10:23:45 GET  /health → 200 OK
2026-02-16 10:24:12 POST /dispense (tx_id=abc123, qty=4) → 200 OK (timeout scenario)
2026-02-16 10:24:15 GET  /dispense/abc123 → 200 OK (state=error, dispensed=2)
```

---

## Future Enhancements

Possible additions (not in initial scope):

- **Configurable timing:** `--dispense-delay=200ms` flag
- **Scenario override:** `--scenario=timeout` to force all requests to use one scenario
- **Metrics endpoint:** `/metrics` for Prometheus scraping
- **Web UI:** Simple HTML dashboard showing current state
- **Scenario randomization:** `--random` flag to randomly select scenarios
- **Persistent history:** Optional `--persist=/tmp/mock-state.json` for crash recovery testing

---

## Success Criteria

The mock is successful if:

1. ✅ Implements full dispenser protocol v1.1.0
2. ✅ Supports all 15 test scenarios
3. ✅ Enables TUI client testing without hardware
4. ✅ Realistic timing simulation (100ms/token)
5. ✅ Easy to start/stop/restart
6. ✅ Clear scenario documentation (`--list-scenarios`)
7. ✅ No external dependencies beyond Go stdlib

---

## References

- [Dispenser Protocol v1.1.0](../dispenser-protocol.md)
- [System Architecture](../../ARCHITECTURE.md)
- [ESP8266 Firmware Design](./2025-02-08-esp8266-firmware-design.md)
