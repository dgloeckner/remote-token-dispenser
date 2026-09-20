# Token Dispenser HTTP API Reference

**Protocol version:** 2 (reported as `protocol` by `GET /health`)
**Version:** 2.0.0
**Base URL:** `http://<ESP8266_IP>` (default: `http://192.168.4.20`)
**Authentication:** API Key via `X-API-Key` header

Complete HTTP API specification for the ESP8266 token dispenser firmware.

**This document is the contract.** The firmware, the Go mock and the terminal
are three implementations of it, and they have drifted apart before without
anyone noticing. Two rules keep them honest:

- A change in behaviour updates this document **in the same pull request**.
- `token-tui conformance` (see [Conformance](#conformance)) turns the document
  into assertions and runs them against the mock in CI and against a real
  device on the bench.

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
- [Conformance](#conformance)
- [Error Codes](#error-codes)
- [Timing & Timeouts](#timing--timeouts)
- [Recovery Scenarios](#recovery-scenarios)

---

## Design Principles

### 1. Idempotency by Transaction ID
Every dispense request includes a client-generated `tx_id` (8-16 character hex string). Repeating the same `tx_id` returns the current state of that transaction without re-dispensing tokens — **including while it is still running**. That is the normal case, not an exotic one: the terminal retries a POST whose answer it did not see, and the transaction it is asking about is the one dispensing at that moment.

Repeating a `tx_id` with a **different quantity** is not a retry; it is a client contradicting itself, and it is refused with `409 tx_id reused`.

**Example:**
```bash
# First request
POST /dispense {"tx_id": "abc123", "quantity": 3}
→ Dispenses 3 tokens

# Retry with same tx_id (network timeout, crash, etc.)
POST /dispense {"tx_id": "abc123", "quantity": 3}
→ Returns cached result: "done", dispensed: 3 (NO additional tokens)
```

### 2. Protocol Version Handshake

`GET /health` reports `"protocol": 2`. A client checks it and **refuses any
other value** rather than adapting to it: there are no devices in the field, so
protocol 2 replaced protocol 1 outright and a mismatch means something was not
deployed, never something to work around at runtime.

```json
{"protocol": 2, "status": "ok", "...": "..."}
```

Protocol 1 was the shape before the conformance suite existed; nothing speaks
it any more.

### 3. Single-Resource Locking
The dispenser is a single physical device. Only one transaction can be active at a time. A request for **another** transaction receives `409 Conflict`; a request for the active one is the idempotent retry of principle 1 and receives `200`.

### 4. Crash-Safe State Persistence
Two memories, because they answer two different questions.

**Flash (EEPROM)** holds one record, written on every state transition: the
active transaction *and* the ring of the last 8 finished ones, sealed with a
magic, a layout version and a CRC-16. A record that fails any of the three is
treated as empty, never as data.

```cpp
{magic, layout_version, crc16,
 active: {tx_id, quantity, dispensed, state, count_reliable},
 ring[8], ring_index}
```

**RTC user memory** holds the live token count, rewritten as each token drops.
It survives a watchdog reset, an exception and a brownout — the resets a motor
starting on a shared supply actually causes — and costs no flash wear. A
commit per token was considered and rejected (owner decision, 2026-09-20): one
extra sector erase per token, to cover only the power-loss case that
`count_reliable` already reports honestly.

On reboot, the firmware:
- Loads the record; the ring comes back with it, so a **finished transaction
  is still a `200` after a reboot** and a `404` means "this request never
  arrived" again
- If it crashed during `dispensing`, it marks the transaction `error` and
  recovers the count:
  - RTC block present and belonging to this `tx_id` → that is the count,
    `count_reliable: true`
  - RTC block gone (a real power loss) → the persisted count is a **lower
    bound**, `count_reliable: false`
- Clients can query final state via `GET /dispense/{tx_id}`

### 4a. `count_reliable`: a count that says how sure it is
`count_reliable` is **required on every transaction response**. There is no
default for a reader to fall back on, on purpose: a missing field would let a
client read "we do not know how many fell" as "we counted zero", which is the
one reading that bills nothing while the tray is full.

- `true` — `dispensed` is exact.
- `false` — `dispensed` is a **lower bound**: at least that many tokens left
  the hopper, possibly more. Bill the bound and put the difference in front of
  a human; do not treat it as a completed reconciliation.

### 5. Dispense-First, Pay-After
Tokens are **physically dispensed before payment processing**. This ensures:
- Exact token count tracking (even during failures)
- No payment refunds for dispense failures
- Simple reconciliation (what was dispensed = what is charged)

### 6. Self-Healing Error Recovery
Hardware errors (from Azkoyen error signal) persist until either:
- **Manual reset:** Power cycle clears error state
- **Self-healing:** Successful token dispense automatically clears active error

Jams (timeout-based) always require manual intervention (power cycle). The dispenser enters `error` state and rejects new requests until reset.

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
| `tx_id` | string | 1-16 character string, client-generated | `"a3f8c012"` |
| `quantity` | integer | 1-20 tokens | `3` |
| `state` | enum | `"idle"`, `"dispensing"`, `"done"`, `"error"` | `"dispensing"` |
| `timestamp` | integer | Seconds since boot (uptime) | `84230` |
| `count_reliable` | boolean | `false` = `dispensed` is a lower bound, not a fact | `true` |

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
  "protocol": 2,
  "status": "ok",
  "uptime": 84230,
  "firmware": "1.1.0",
  "wifi": {
    "rssi": -47,
    "ip": "192.168.188.243",
    "ssid": "Ponyhof"
  },
  "dispenser": "idle",
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
  },
  "error": {
    "active": false
  },
  "error_history": []
}
```

**Response (200 OK) - With Active Error:**
```json
{
  "protocol": 2,
  "status": "ok",
  "uptime": 84230,
  "firmware": "1.1.0",
  "wifi": {
    "rssi": -47,
    "ip": "192.168.188.243",
    "ssid": "Ponyhof"
  },
  "dispenser": "error",
  "gpio": {
    "coin_pulse": {"raw": 1, "active": false},
    "error_signal": {"raw": 0, "active": true},
    "hopper_low": {"raw": 1, "active": false}
  },
  "metrics": {
    "total_dispenses": 1247,
    "successful": 1189,
    "jams": 3,
    "partial": 2,
    "failures": 55
  },
  "error": {
    "active": true,
    "code": 3,
    "type": "JAM_PERMANENT",
    "timestamp": 82150,
    "description": "Permanent jam detected"
  },
  "error_history": [
    {
      "code": 3,
      "type": "JAM_PERMANENT",
      "timestamp": 82150,
      "cleared": false
    },
    {
      "code": 1,
      "type": "COIN_STUCK",
      "timestamp": 75400,
      "cleared": true
    }
  ]
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `protocol` | integer | Protocol version; always `2`. A client that reads anything else refuses the device. |
| `status` | string | Overall health: `"ok"`, `"degraded"`, `"error"` |
| `uptime` | integer | Seconds since boot |
| `firmware` | string | Firmware version |
| `wifi` | object | WiFi connection info |
| `wifi.rssi` | integer | Signal strength in dBm (-30 to -90) |
| `wifi.ip` | string | ESP8266 IP address |
| `wifi.ssid` | string | Connected WiFi network name |
| `dispenser` | string | Current state: `"idle"`, `"dispensing"`, `"error"` |
| `gpio` | object | GPIO pin states |
| `gpio.coin_pulse` | object | Coin sensor state (`raw`: pin value, `active`: interpreted state) |
| `gpio.error_signal` | object | Error signal state (`raw`: pin value, `active`: interpreted state) |
| `gpio.hopper_low` | object | Hopper low sensor state (`raw`: pin value, `active`: interpreted state) |
| `metrics` | object | Dispense metrics |
| `metrics.total_dispenses` | integer | Total dispense attempts since boot |
| `metrics.successful` | integer | Completed successfully |
| `metrics.jams` | integer | Jam errors detected (timeout-based) |
| `metrics.partial` | integer | Partial dispenses (subset of jams) |
| `metrics.failures` | integer | Total failures (jams + other errors) |
| `error` | object | Active error information |
| `error.active` | boolean | Whether an error is currently active |
| `error.code` | integer | Error code (1-7, see Error Codes section) |
| `error.type` | string | Error type name (e.g., "JAM_PERMANENT") |
| `error.timestamp` | integer | Uptime when error was detected |
| `error.description` | string | Human-readable error description |
| `error_history` | array | Last 5 error events (newest first) |
| `error_history[].code` | integer | Error code (1-7) |
| `error_history[].type` | string | Error type name |
| `error_history[].timestamp` | integer | Uptime when error was detected |
| `error_history[].cleared` | boolean | Whether error has been cleared |

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
  "dispensed": 0,
  "count_reliable": true
}
```

**Response (200 OK) - Idempotent (already exists):**
```json
{
  "tx_id": "a3f8c012",
  "state": "done",
  "quantity": 3,
  "dispensed": 3,
  "count_reliable": true
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
- **Another** transaction is currently `dispensing` — `active_tx_id` names it,
  and it is never the `tx_id` of the request itself
- Dispenser is in `error` state (jam, requires reset)

**Response (409 Conflict) - Reused transaction ID:**
```json
{
  "error": "tx_id reused"
}
```

Returned when `tx_id` is already known (active or in the history ring) but
`quantity` differs from the one it was started with.

**Response (413 Payload Too Large) - Body over 256 bytes:**
```json
{
  "error": "body too large"
}
```

A dispense request is about 40 bytes. Anything past 256 is refused outright
rather than truncated into a parse error that blames the JSON.

**Response (415 Unsupported Media Type) - Wrong Content-Type:**
```json
{
  "error": "content-type must be application/json"
}
```

**Response (400 Bad Request) - Empty body:**
```json
{
  "error": "empty body"
}
```

Every POST is answered. A request without a body is a client bug, and a client
bug must produce a status code, not silence: an unanswered request costs the
caller its whole timeout and then looks exactly like a lost one.

**Request framing:**

The body may arrive in any number of TCP segments. The device assembles it
before parsing — `Content-Length` is what says how much to wait for, and a
device that parses the first segment it receives rejects perfectly valid
requests whenever the network happens to split them.


**Behavior:**

1. **New transaction:** If dispenser is `idle` and `tx_id` is new:
   - Transition to `dispensing` state
   - Return `200` with `state: "dispensing"` and `dispensed: 0`, answered from
     memory
   - Persist to flash and start the motor **within 10 ms, after the answer**

   The order matters to the caller, not just to the device. The answer does not
   wait for a flash sector erase or for the serial log; the POST is a decision,
   and the work follows it. A device that does the work first takes seconds to
   answer, and the terminal retries a transaction that is already running
   (issue #4).

   The window this opens is a reset between the answer and the flash write.
   Nothing has been persisted and no token has dropped, so the transaction is
   forgotten: a retry of the same `tx_id` then starts it for real, and a
   `GET /dispense/{tx_id}` in between answers `404`. That is the safe
   direction — the opposite, a flash record for a dispense that never ran,
   would bill tokens nobody received.

2. **Idempotent retry:** If `tx_id` is the transaction currently running, or
   one in the history ring, and `quantity` is the one it was started with:
   - Return its state (`dispensing`, `done`, or `error`)
   - No additional tokens dispensed, and the active transaction is untouched
   - Safe to retry on network failures

3. **Reused `tx_id`:** If `tx_id` is known but `quantity` differs:
   - Return `409 Conflict` with `{"error": "tx_id reused"}`
   - Answering with the stored quantity would look like the confirmation of a
     request that was never made

4. **Busy/Error:** If **another** transaction is dispensing, or the dispenser is
   in `error`:
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
  "dispensed": 2,
  "count_reliable": true
}
```

**Response (200 OK) - Recovered after a power loss:**
```json
{
  "tx_id": "a3f8c012",
  "state": "error",
  "quantity": 5,
  "dispensed": 0,
  "count_reliable": false
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `tx_id` | string | Transaction ID |
| `state` | string | Current state: `"dispensing"`, `"done"`, `"error"` |
| `quantity` | integer | Requested token count |
| `dispensed` | integer | Actual tokens dispensed so far |
| `count_reliable` | boolean | **Required.** `false` means `dispensed` is a lower bound (see Design Principle 4a) |

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
  "error": "not found"
}
```

Transaction not found means:
- `tx_id` never existed — **including across a reboot**: the history ring is
  persisted, so a reboot does not turn a finished transaction into a `404`
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

Errors can be cleared in two ways:

1. **Self-healing (hardware errors only):**
   - When a successful dispense completes (`dispensing` → `done`)
   - Active hardware error is automatically cleared
   - Dispenser returns to `idle` and accepts new requests
   - This handles transient hardware issues

2. **Manual reset (all errors):**
   - Operator physically clears jam/issue
   - Power cycle (reboot) ESP8266
   - On boot, ESP8266 clears `error` state → returns to `idle`
   - Required for jam timeouts and persistent hardware faults

---

## Conformance

The suite that decides whether an implementation speaks this protocol:

```sh
# the Go mock (runs in CI)
token-tui conformance --endpoint http://127.0.0.1:8080 --api-key dev --target mock

# a real ESP8266 with the hopper simulator (docs/hopper-simulator.md)
token-tui conformance --endpoint http://192.168.4.20 --api-key … --target simulator \
  --interactive --json report.json
```

Same binary, same table of cases, both targets. The exit code is the verdict;
`--json` writes a per-case report. Cases that need a physical act (press RST,
cut power, flip a simulator switch) are skipped unless `--interactive` is
given, and a case that leaves the device in a state only a power cycle clears
runs last.

The table lives in `dispenser-client-tui/conformance_cases.go`. Adding to it:

- **Assert the protocol, not an implementation.** If the firmware and the mock
  disagree, that is the finding — the case does not get weakened until both
  pass.
- A case that is known to fail against one target carries a `Note` naming the
  issue that will fix it, so a red line reads as *known* or *new* at a glance.
- Known-red today: `post_while_error_is_409` (firmware accepts a dispense while
  a hardware error is active — issue #6). It is green against the mock.
- The two resets of Design Principle 4 are covered from both sides: the mock
  runs `crashed_tx_is_found_after_reboot` and `power_loss_reports_count_unreliable`
  in CI (its scenarios for quantity 5 and 17), and on a real device the same
  two cases are `reset_mid_dispense_reports_partial_count` (press RST) and
  `power_loss_mid_dispense_reports_count_unreliable` (cut the power), which
  need `--interactive`.

Native unit tests cover the firmware logic that needs no network
(`firmware/dispenser/test/`); the conformance suite covers everything that only
exists once the HTTP layer and the hardware are in play.

---

## Error Codes

### HTTP Error Codes

| Status Code | Error | Description | Resolution |
|-------------|-------|-------------|------------|
| `400` | `invalid tx_id or quantity` | Missing fields, invalid length/range | Fix request format |
| `400` | `invalid url` | Malformed URL path | Check URL format |
| `400` | `invalid request format` | JSON type mismatch | Check field types |
| `400` | `empty body` | POST without a body | Send the JSON body |
| `400` | `incomplete body` | Body shorter than `Content-Length` | Send the whole body |
| `401` | `unauthorized` | Missing or invalid API key | Add/fix `X-API-Key` header |
| `404` | `not found` | Unknown `tx_id` | Check tx_id, may have expired |
| `409` | `busy` | Another transaction active | Wait and retry |
| `409` | `tx_id reused` | Known `tx_id`, different `quantity` | Use a fresh `tx_id` |
| `409` | `error` (dispenser in error state) | Jam or hardware fault | Clear jam, power cycle |
| `413` | `body too large` | Body over 256 bytes | Send a dispense request, nothing else |
| `415` | `content-type must be application/json` | Wrong/missing Content-Type | Set `Content-Type: application/json` |

### Azkoyen Hardware Error Codes

These error codes are reported via the error signal pin (GPIO D5) from the Azkoyen Hopper. See `GET /health` endpoint for active error and error history.

| Code | Type | Description | Cause | Resolution |
|------|------|-------------|-------|------------|
| `1` | `COIN_STUCK` | Coin stuck in exit sensor (>65ms) | Token jammed at exit sensor | Clear exit path, power cycle |
| `2` | `SENSOR_OFF` | Exit sensor stuck OFF | Sensor blocked or misaligned | Clean/adjust sensor, power cycle |
| `3` | `JAM_PERMANENT` | Permanent jam detected | Hopper mechanism jammed | Clear jam, power cycle |
| `4` | `MAX_SPAN` | Multiple spans exceeded max time | Motor running too long without dispensing | Check hopper load, power cycle |
| `5` | `MOTOR_FAULT` | Motor doesn't start | Motor or driver failure | Check 12V power, motor connection |
| `6` | `SENSOR_FAULT` | Exit sensor disconnected/faulty | Sensor wiring issue | Check sensor connection |
| `7` | `POWER_FAULT` | Power supply out of range | 12V supply voltage abnormal | Check power supply voltage |

**Error Signal Protocol:**
- Errors are pulse-encoded: 100ms start pulse + N×10ms pulses (where N = error code)
- Active errors persist until cleared by successful dispense or power cycle
- Error history tracks last 5 errors with timestamps and cleared status

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

### ESP8266 Reset Mid-Dispense

**Scenario:** the ESP8266 resets while the motor is running. Which reset it was
decides what the count is worth, so they are two scenarios, not one.

**A. Watchdog reset, exception, brownout — RTC memory survives**
1. ESP8266 reboots
2. Firmware loads the persisted record; the history ring comes back with it
3. Detects `state == "dispensing"`
4. Reads the live count from the RTC block, which belongs to this `tx_id`
5. Marks the transaction `error` with that **exact** count and
   `count_reliable: true`

**Result:** every token that reached the tray is billed.

**B. Real power loss — RTC memory is gone**
1.–3. as above
4. The RTC block is absent or belongs to another transaction
5. Marks the transaction `error` with the count from flash — the zero written
   at the start, or whatever the last transition stored — and
   `count_reliable: false`

**Result:** a lower bound that says it is one. The terminal bills the bound and
puts the difference in front of a human. This is the case a flash commit per
token would have covered; it was rejected in favour of reporting it honestly.

**After either:** the transaction stays in the persisted ring, so
`GET /dispense/{tx_id}` answers `200` after the reboot — and a `404` keeps its
one meaning, "the request never arrived".

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

### Self-Healing Hardware Error

**Scenario:** Hopper reports transient hardware error (e.g., COIN_STUCK), but clears itself.

**Detection:**
1. ESP8266 detects error signal from hopper
2. Error decoder identifies error code (e.g., code 1 = COIN_STUCK)
3. Error recorded in history with `cleared: false`
4. Health endpoint reports active error

**Client Response:**
```bash
$ curl http://192.168.4.20/health
{
  "dispenser": "idle",
  "error": {
    "active": true,
    "code": 1,
    "type": "COIN_STUCK",
    "timestamp": 75400,
    "description": "Coin stuck in exit sensor (>65ms)"
  }
}
```

**Self-Healing:**
1. Client initiates new dispense transaction
2. Dispense completes successfully (`dispensing` → `done`)
3. ESP8266 automatically clears active error
4. Error marked as `cleared: true` in history
5. Dispenser continues normal operation

```bash
$ curl http://192.168.4.20/health
{
  "dispenser": "idle",
  "error": {
    "active": false
  },
  "error_history": [
    {
      "code": 1,
      "type": "COIN_STUCK",
      "timestamp": 75400,
      "cleared": true
    }
  ]
}
```

**Resolution:** No manual intervention required. System self-healed.

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

# 4. Retry while the transaction is still running
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"abc123","quantity":3}'

{"tx_id":"abc123","state":"dispensing","quantity":3,"dispensed":1}
# ↑ The caller's own transaction — 200 with its progress, never 409 busy

# 5. Same tx_id, different quantity
$ curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: secret" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"abc123","quantity":5}'

409 Conflict
{"error":"tx_id reused"}
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
- **GPIO Pins:**
  - D1 (GPIO5): Motor control output
  - D7 (GPIO13): Coin pulse input (FALLING edge interrupt)
  - D5 (GPIO14): Error signal input
  - D6 (GPIO12): Hopper low sensor input

### Ring Buffer

The ESP8266 keeps the last **8 transactions** for idempotency, and keeps them
**in flash**: the ring is part of the persisted record, so it survives a
reboot. When the 9th transaction arrives it overwrites the oldest entry.
Clients should not reuse transaction IDs from more than 8 transactions ago.

### Flash Persistence Format

```cpp
struct PersistedTransaction {
  char tx_id[17];          // Null-terminated transaction ID
  uint8_t quantity;        // Requested count
  uint8_t dispensed;       // Actual count
  uint8_t state;           // TransactionState, as one byte
  uint8_t count_reliable;  // 1 = dispensed is exact
};

struct PersistedRecord {
  uint32_t magic;          // "F3TX"
  uint16_t layout_version;
  uint16_t crc16;          // over the record with this field zeroed
  PersistedTransaction active;      // state IDLE = no active transaction
  PersistedTransaction ring[8];
  uint8_t ring_index;
  uint8_t reserved[3];
};
```

Written to EEPROM address 0 on every state transition — **one commit, not one
per token**. A successful dispense costs exactly two: the start and the finish.
The record is no longer cleared on completion; the ring *is* the record.

`state` is a `uint8_t` and not the enum: this struct is a wire format between
two builds of the firmware, and the width of an enum is not promised by the
language. Magic, layout version and CRC are all three checked on load, and a
record that fails any of them is empty, never data.

### RTC Count Block

```cpp
struct RtcCountBlock {
  uint32_t magic;          // "F3RC"
  char tx_id[20];
  uint32_t dispensed;
  uint32_t crc;
};
```

Written to RTC user memory as each token drops. A block whose `tx_id` is not
the one being recovered is ignored — it belongs to an earlier customer.

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

### Version 2.0.0 (2026-09-20) — protocol 2
- **Version handshake:** `GET /health` reports `"protocol": 2`; clients refuse
  any other value instead of adapting to it (no devices are in the field)
- **This document is the contract:** behaviour changes update it in the same PR
- **Conformance suite:** `token-tui conformance` runs the cases below against
  the mock (in CI) and against a real device
- **Idempotency of the active transaction (#2):** a POST for the transaction
  that is dispensing returns its state (`200`) instead of `409 busy`, an
  idempotent hit no longer replaces the active transaction, and a known `tx_id`
  with a different `quantity` is refused with `409 tx_id reused`
- **The count and the history survive a reset (#3):** the live token count is
  kept in RTC user memory and recovered after a watchdog reset, an exception or
  a brownout, so a reset mid-dispense no longer reports `dispensed: 0` while
  the tray is full; a real power loss is reported as a lower bound with
  `count_reliable: false`. `count_reliable` is now a **required field on every
  transaction response**. The history ring is persisted with the active
  transaction, so a finished transaction is still found after a reboot and a
  `404` means "never arrived" again. The flash record carries a magic, a layout
  version and a CRC-16, and a completed dispense costs two commits instead of
  two plus an erase.
- Known deviation of the firmware from this document: issue #6 (dispense while
  a hardware error is active); the suite reports it per case rather than
  hiding it

### Version 1.1.0 (2026-02-14)
- **Error decoding:** Added Azkoyen hardware error code detection (7 error types)
- **Error history:** Track last 5 errors with timestamps and cleared status
- **Self-healing:** Successful dispenses automatically clear active hardware errors
- **Enhanced health endpoint:** Added `error` and `error_history` fields
- **Improved GPIO monitoring:** Raw and interpreted pin states in `/health`

### Version 1.0.0 (2025-02-08)
- Initial API specification
- Simplified single-phase dispense model
- Persistent error states with power cycle reset
- ArduinoJson 7.x compatibility
- Comprehensive input validation

---

**Hardware Manual:** [Azkoyen Hopper U-II PDF](https://www.casino-software.de/download/hopper-azkoyen-u2-manual.pdf)

**System Architecture:** See [ARCHITECTURE.md](ARCHITECTURE.md) for complete system design and integration patterns.
