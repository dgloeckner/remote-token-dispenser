# Token Dispenser HTTP API Reference

**Version:** 1.0.0
**Base URL:** `http://<ESP8266_IP>` (default: `http://192.168.4.20`)
**Authentication:** API Key via `X-API-Key` header

Complete HTTP API specification for the ESP8266 token dispenser firmware.

---

## Table of Contents

- [Design Principles](#design-principles)
- [Authentication](#authentication)
- [Data Types](#data-types)
- [Endpoints](#endpoints)
  - [GET /health](#get-health)
  - [POST /dispense](#post-dispense)
  - [GET /dispense/{tx_id}](#get-dispensetx_id)
- [State Machine](#state-machine)
- [Error Codes](#error-codes)
- [Timing & Timeouts](#timing--timeouts)
- [Recovery Scenarios](#recovery-scenarios)

---

## Design Principles

### 1. Idempotency by Transaction ID
Every dispense request includes a client-generated `tx_id` (8-16 character hex string). Repeating the same `tx_id` returns the cached result without re-dispensing tokens.

**Example:**
```bash
# First request
POST /dispense {"tx_id": "abc123", "quantity": 3}
→ Dispenses 3 tokens

# Retry with same tx_id (network timeout, crash, etc.)
POST /dispense {"tx_id": "abc123", "quantity": 3}
→ Returns cached result: "done", dispensed: 3 (NO additional tokens)
```

### 2. Single-Resource Locking
The dispenser is a single physical device. Only one transaction can be active at a time. Concurrent requests receive `409 Conflict`.

### 3. Crash-Safe State Persistence
The ESP8266 persists transaction state to flash memory on every state transition:
```cpp
{tx_id: "abc123", quantity: 3, dispensed: 2, state: "dispensing"}
```

On reboot, the firmware:
- Loads persisted state
- If crashed during `dispensing` → marks as `error` with exact partial count
- Clients can query final state via `GET /dispense/{tx_id}`

### 4. Dispense-First, Pay-After
Tokens are **physically dispensed before payment processing**. This ensures:
- Exact token count tracking (even during failures)
- No payment refunds for dispense failures
- Simple reconciliation (what was dispensed = what is charged)

### 5. Persistent Errors
Jams and hardware errors persist until manual intervention (power cycle). The dispenser enters `error` state and rejects new requests until reset.

---

## Authentication

### Protected Endpoints
The following endpoints require API key authentication:
- `POST /dispense`
- `GET /dispense/{tx_id}`

**Authentication Header:**
```http
X-API-Key: your-secret-api-key-here
```

**Unauthorized Response (401):**
```json
{
  "error": "unauthorized"
}
```

### Public Endpoints
The following endpoints do NOT require authentication:
- `GET /health` - Used for monitoring and health checks

---

## Data Types

| Type | Format | Description | Example |
|------|--------|-------------|---------|
| `tx_id` | string | 8-16 hex characters, client-generated | `"a3f8c012"` |
| `quantity` | integer | 1-20 tokens | `3` |
| `state` | enum | `"idle"`, `"dispensing"`, `"done"`, `"error"` | `"dispensing"` |
| `timestamp` | integer | Seconds since boot (uptime) | `84230` |

---

## Endpoints

### GET /health

Health status and metrics for monitoring.

**Authentication:** None required

**Request:**
```http
GET /health HTTP/1.1
Host: 192.168.4.20
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "uptime": 84230,
  "firmware": "1.0.0",
  "wifi": {
    "rssi": -47,
    "ip": "192.168.188.243",
    "ssid": "Ponyhof"
  },
  "dispenser": "idle",
  "hopper_low": false,
  "gpio": {
    "coin_pulse": {"raw": 1, "active": false},
    "error_signal": {"raw": 1, "active": false},
    "hopper_low": {"raw": 1, "active": false}
  },
  "metrics": {
    "total_dispenses": 1247,
    "successful": 1189,
    "jams": 3,
    "partial": 2,
    "failures": 55
  }
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Overall health: `"ok"`, `"degraded"`, `"error"` |
| `uptime` | integer | Seconds since boot |
| `firmware` | string | Firmware version |
| `wifi` | object | WiFi connection info (optional) |
| `wifi.rssi` | integer | Signal strength in dBm (-30 to -90) |
| `wifi.ip` | string | ESP8266 IP address |
| `wifi.ssid` | string | Connected WiFi network name |
| `dispenser` | string | Current state: `"idle"`, `"dispensing"`, `"error"` |
| `hopper_low` | boolean | Low token warning (optical sensor) |
| `gpio` | object | GPIO pin states (optional) |
| `gpio.coin_pulse` | object | Coin sensor state |
| `gpio.error_signal` | object | Error signal state |
| `gpio.hopper_low` | object | Hopper low sensor state |
| `metrics.total_dispenses` | integer | Total dispense attempts since boot |
| `metrics.successful` | integer | Completed successfully |
| `metrics.jams` | integer | Jam errors detected |
| `metrics.partial` | integer | Partial dispenses (subset of jams) |
| `metrics.failures` | integer | Total failures (jams + other errors) |

**Status Levels:**
- `"ok"` - Operating normally, dispenser idle or active
- `"degraded"` - Hopper low warning
- `"error"` - Active jam/fault, dispenser unavailable

**Usage:**
System monitors should poll this endpoint every 60 seconds to track:
- Success rate: `successful / total_dispenses`
- Jam rate: `jams / total_dispenses`
- Dispenser availability: `dispenser != "error"`

---

### POST /dispense

Start a token dispense transaction.

**Authentication:** Required (`X-API-Key` header)

**Request:**
```http
POST /dispense HTTP/1.1
Host: 192.168.4.20
X-API-Key: your-secret-api-key-here
Content-Type: application/json

{
  "tx_id": "a3f8c012",
  "quantity": 3
}
```

**Request Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `tx_id` | string | Yes | Unique transaction ID (8-16 hex chars) |
| `quantity` | integer | Yes | Number of tokens (1-20) |

**Response (200 OK) - Started:**
```json
{
  "tx_id": "a3f8c012",
  "state": "dispensing",
  "quantity": 3,
  "dispensed": 0
}
```

**Response (200 OK) - Idempotent (already exists):**
```json
{
  "tx_id": "a3f8c012",
  "state": "done",
  "quantity": 3,
  "dispensed": 3
}
```

**Response (400 Bad Request) - Invalid input:**
```json
{
  "error": "invalid tx_id or quantity"
}
```

Causes:
- Missing `tx_id` or `quantity`
- `tx_id` length not 1-16 characters
- `quantity` not in range 1-20

**Response (401 Unauthorized) - Missing/invalid API key:**
```json
{
  "error": "unauthorized"
}
```

**Response (409 Conflict) - Busy:**
```json
{
  "error": "busy",
  "active_tx_id": "previous_tx",
  "active_state": "dispensing"
}
```

Returned when:
- Another transaction is currently `dispensing`
- Dispenser is in `error` state (jam, requires reset)

**Response (415 Unsupported Media Type) - Wrong Content-Type:**
```json
{
  "error": "content-type must be application/json"
}
```

**Behavior:**

1. **New transaction:** If dispenser is `idle` and `tx_id` is new:
   - Transition to `dispensing` state
   - Persist to flash
   - Start motor
   - Return `200` with `state: "dispensing"`

2. **Idempotent retry:** If `tx_id` already exists in history:
   - Return cached state (`dispensing`, `done`, or `error`)
   - No additional tokens dispensed
   - Safe to retry on network failures

3. **Busy/Error:** If dispenser not idle:
   - Return `409 Conflict`
   - Client should retry after delay or check status

---

### GET /dispense/{tx_id}

Query transaction status by transaction ID.

**Authentication:** Required (`X-API-Key` header)

**Request:**
```http
GET /dispense/a3f8c012 HTTP/1.1
Host: 192.168.4.20
X-API-Key: your-secret-api-key-here
```

**URL Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `tx_id` | string | Transaction ID to query (1-16 chars) |

**Response (200 OK) - Found:**
```json
{
  "tx_id": "a3f8c012",
  "state": "dispensing",
  "quantity": 3,
  "dispensed": 2
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `tx_id` | string | Transaction ID |
| `state` | string | Current state: `"dispensing"`, `"done"`, `"error"` |
| `quantity` | integer | Requested token count |
| `dispensed` | integer | Actual tokens dispensed so far |

**Response (400 Bad Request) - Invalid tx_id:**
```json
{
  "error": "invalid tx_id"
}
```

**Response (401 Unauthorized) - Missing/invalid API key:**
```json
{
  "error": "unauthorized"
}
```

**Response (404 Not Found) - Unknown transaction:**
```json
{
  "error": "transaction not found"
}
```

Transaction not found means:
- `tx_id` never existed
- Transaction expired from history (ring buffer overflow after 8+ new transactions)

**Usage:**

Poll this endpoint to track dispense progress:

```bash
# Start dispense
POST /dispense {"tx_id": "abc123", "quantity": 5}
→ 200 {"state": "dispensing", "dispensed": 0}

# Poll every 250ms
GET /dispense/abc123
→ 200 {"state": "dispensing", "dispensed": 1}

GET /dispense/abc123
→ 200 {"state": "dispensing", "dispensed": 3}

GET /dispense/abc123
→ 200 {"state": "done", "dispensed": 5}
```

**The `dispensed` field updates in real-time** as tokens drop (via GPIO interrupt).

---

## State Machine

### Transaction States

```
idle ──POST /dispense──► dispensing ──[success]──► done
                              │
                              │[jam/timeout]
                              ▼
                           error
```

**State Descriptions:**

| State | Description | Next States | Actions |
|-------|-------------|-------------|---------|
| `idle` | Ready for new transaction | `dispensing` | Motor off, no active tx |
| `dispensing` | Motor running, counting tokens | `done`, `error` | Motor on, interrupt counting |
| `done` | Completed successfully | - | Motor off, count matched |
| `error` | Jam or hardware fault | - | Motor off, persistent until reset |

**State Transitions:**

1. **idle → dispensing:**
   - Trigger: `POST /dispense` with new `tx_id`
   - Actions: Start motor, reset pulse counter, persist state
   - Duration: ~2.5 seconds per token

2. **dispensing → done:**
   - Trigger: `dispensed >= quantity`
   - Actions: Stop motor, persist final state, add to history
   - Result: Successful completion

3. **dispensing → error:**
   - Trigger: Jam timeout (5 seconds without pulse)
   - Actions: Stop motor, persist state with partial count
   - Result: Requires manual reset (power cycle)

**Error State Recovery:**

Errors are **persistent** - the dispenser stays in `error` state until:
1. Operator physically clears jam
2. Power cycle (reboot)
3. On boot, ESP8266 clears `error` state → returns to `idle`

---

## Error Codes

| Status Code | Error | Description | Resolution |
|-------------|-------|-------------|------------|
| `400` | `invalid tx_id or quantity` | Missing fields, invalid length/range | Fix request format |
| `400` | `invalid url` | Malformed URL path | Check URL format |
| `400` | `invalid request format` | JSON type mismatch | Check field types |
| `401` | `unauthorized` | Missing or invalid API key | Add/fix `X-API-Key` header |
| `404` | `transaction not found` | Unknown `tx_id` | Check tx_id, may have expired |
| `409` | `busy` | Another transaction active | Wait and retry |
| `409` | `error` (dispenser in error state) | Jam or hardware fault | Clear jam, power cycle |
| `415` | `content-type must be application/json` | Wrong/missing Content-Type | Set `Content-Type: application/json` |

---

## Timing & Timeouts

### Hardware Timing

| Parameter | Value | Description |
|-----------|-------|-------------|
| Token dispense rate | ~2.5 seconds/token | Azkoyen Hopper U-II mechanical speed |
| Pulse width | 30ms | Opto-sensor pulse duration per token |
| Pulse detection | FALLING edge | GPIO interrupt trigger |

### Software Timeouts

| Timeout | Value | Description |
|---------|-------|-------------|
| Jam detection | 5 seconds | No pulse received → jam error |
| History retention | 8 transactions | Ring buffer size for idempotency |

**Jam Detection Logic:**
```
If (millis() - last_pulse_time > 5000ms):
    → Stop motor
    → State = error
    → Persist partial count
    → Require power cycle reset
```

**Expected Dispense Duration:**
- 1 token: ~2.5 seconds
- 5 tokens: ~12.5 seconds
- 20 tokens: ~50 seconds

---

## Recovery Scenarios

### Client Crash Mid-Dispense

**Scenario:** Client sends `POST /dispense`, then crashes before reading response.

**Recovery:**
1. Client reboots
2. Checks local database for incomplete transactions
3. Queries `GET /dispense/{tx_id}` for each
4. ESP8266 returns current state:
   - `dispensing` → still in progress, keep polling
   - `done` → completed, update local DB
   - `error` → partial dispense, record exact count

**Result:** Exact `dispensed` count preserved, no double-dispensing.

---

### ESP8266 Crash Mid-Dispense

**Scenario:** Power loss to ESP8266 while motor running.

**Recovery:**
1. ESP8266 reboots
2. Firmware loads persisted state from flash
3. Detects `state == "dispensing"` on boot
4. Marks as `error` state (unknown partial count)
5. Client queries `GET /dispense/{tx_id}` → sees `error` with last known `dispensed` count

**Result:** Partial count recorded, transaction marked as failed.

---

### Network Timeout

**Scenario:** Client sends `POST /dispense`, network drops, no response received.

**Recovery:**
1. Client retries same `tx_id`
2. ESP8266 recognizes duplicate `tx_id` in history
3. Returns current state (may have progressed to `done`)
4. Client receives status without triggering new dispense

**Result:** Safe retry, idempotency prevents double-dispensing.

---

### Jam During Dispense

**Scenario:** Motor jammed after 2 of 5 tokens dispensed.

**Detection:**
1. Watchdog timer: no pulse for 5 seconds
2. ESP8266 detects jam
3. Stops motor
4. State → `error`, `dispensed = 2`
5. Persists to flash

**Client Response:**
```json
{
  "tx_id": "abc123",
  "state": "error",
  "quantity": 5,
  "dispensed": 2
}
```

**Resolution:**
1. Operator physically clears jam
2. Power cycle ESP8266
3. On boot, ESP8266 clears error → returns to `idle`
4. Client records partial dispense in local DB
5. Backend reconciliation handles refund/credit

---

### Concurrent Requests

**Scenario:** Two clients send `POST /dispense` simultaneously.

**Behavior:**
1. First request: Accepted, state → `dispensing`
2. Second request: Rejected with `409 Conflict`:
   ```json
   {
     "error": "busy",
     "active_tx_id": "first_tx",
     "active_state": "dispensing"
   }
   ```

**Resolution:**
1. Second client waits
2. Polls `GET /health` to check `dispenser` state
3. When `dispenser == "idle"`, retries original request

---

## Polling Best Practices

### During Dispense

Poll `GET /dispense/{tx_id}` every **250ms** until completion:

```bash
while true; do
  response=$(curl -H "X-API-Key: key" http://192.168.4.20/dispense/abc123)
  state=$(echo $response | jq -r '.state')

  if [ "$state" == "done" ] || [ "$state" == "error" ]; then
    break
  fi

  sleep 0.25
done
```

### For Monitoring

Poll `GET /health` every **60 seconds**:

```bash
while true; do
  curl http://192.168.4.20/health
  sleep 60
done
```

---

## Example Flows

### Successful Dispense

```bash
# 1. Start dispense
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"abc123","quantity":3}'

{"tx_id":"abc123","state":"dispensing","quantity":3,"dispensed":0}

# 2. Poll status
$ curl -H "X-API-Key: secret" http://192.168.4.20/dispense/abc123
{"tx_id":"abc123","state":"dispensing","quantity":3,"dispensed":1}

$ curl -H "X-API-Key: secret" http://192.168.4.20/dispense/abc123
{"tx_id":"abc123","state":"dispensing","quantity":3,"dispensed":2}

$ curl -H "X-API-Key: secret" http://192.168.4.20/dispense/abc123
{"tx_id":"abc123","state":"done","quantity":3,"dispensed":3}

# 3. Idempotent retry (same tx_id)
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"abc123","quantity":3}'

{"tx_id":"abc123","state":"done","quantity":3,"dispensed":3}
# ↑ Returns cached result, no additional tokens dispensed
```

### Concurrent Conflict

```bash
# Client 1: Start dispense
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -d '{"tx_id":"tx1","quantity":5}'

{"tx_id":"tx1","state":"dispensing","quantity":5,"dispensed":0}

# Client 2: Try to dispense (immediately)
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -d '{"tx_id":"tx2","quantity":2}'

409 Conflict
{"error":"busy","active_tx_id":"tx1","active_state":"dispensing"}

# Client 2: Wait for Client 1 to finish, then retry
```

### Jam Recovery

```bash
# 1. Start dispense
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -d '{"tx_id":"jam123","quantity":5}'

{"tx_id":"jam123","state":"dispensing","quantity":5,"dispensed":0}

# 2. [Physical jam occurs after 2 tokens]

# 3. Poll shows error
$ curl -H "X-API-Key: secret" http://192.168.4.20/dispense/jam123
{"tx_id":"jam123","state":"error","quantity":5,"dispensed":2}

# 4. Check health (dispenser in error state)
$ curl http://192.168.4.20/health
{"dispenser":"error",...}

# 5. Operator clears jam, power cycles ESP8266

# 6. After reboot, dispenser returns to idle
$ curl http://192.168.4.20/health
{"dispenser":"idle",...}

# 7. Client can now start new transactions
```

---

## Implementation Notes

### ESP8266 Firmware

- **Platform:** Wemos D1 Mini (ESP8266)
- **Framework:** Arduino
- **Libraries:** ESPAsyncWebServer, ArduinoJson 7.x
- **Memory:** EEPROM for crash recovery (64 bytes per transaction)
- **GPIO:** D6 (GPIO12) interrupt for pulse counting

### Ring Buffer

The ESP8266 maintains a ring buffer of the last **8 transactions** for idempotency:

```cpp
struct History {
  char tx_id[8][17];
  TransactionState states[8];
  uint8_t index;  // Circular index
};
```

When the 9th transaction arrives, it overwrites the oldest entry. Clients should not reuse transaction IDs from more than 8 transactions ago.

### Flash Persistence Format

```cpp
struct PersistedTransaction {
  uint8_t magic;           // 0xAB validation byte
  char tx_id[17];          // Null-terminated transaction ID
  uint8_t quantity;        // Requested count
  uint8_t dispensed;       // Actual count
  TransactionState state;  // Current state
};
```

Written to EEPROM address 0-64 on every state transition.

---

## Security Considerations

### API Key Security

- API key transmitted in plain HTTP (no TLS)
- Acceptable for **local network deployment only**
- Do not expose ESP8266 to public internet
- Change default API key before production use

### Rate Limiting

No built-in rate limiting. Physical dispense rate (~2.5s/token) provides natural throttling.

### Input Validation

All inputs validated:
- `tx_id` length: 1-16 characters
- `quantity` range: 1-20
- JSON type checking
- Content-Type enforcement

---

## Changelog

### Version 1.0.0 (2025-02-08)
- Initial API specification
- Simplified single-phase dispense model
- Persistent error states with power cycle reset
- ArduinoJson 7.x compatibility
- Comprehensive input validation

---

**Hardware Manual:** [Azkoyen Hopper U-II PDF](https://www.casino-software.de/download/hopper-azkoyen-u2-manual.pdf)

**System Architecture:** See [ARCHITECTURE.md](ARCHITECTURE.md) for complete system design and integration patterns.
