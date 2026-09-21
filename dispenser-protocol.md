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
  - [GET /debug](#get-debug)
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
{"protocol": 2, "state": "idle", "fault": "none", "...": "..."}
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

### 4b. `dispensed` may be greater than `quantity`
A token that is already past the wheel when the motor is cut still falls. The
firmware therefore keeps counting for a short **settling window** (500 ms)
after the target count is reached, and reports what actually came out — even
when that is one more than was asked for.

- `dispensed > quantity` is **legal** and means exactly that: more tokens left
  the hopper than were requested. The terminal **bills what came out**.
- The transaction is still `done`. An overrun is an accounting fact, not a
  fault; nothing was lost and nothing jammed.
- `metrics.overrun_tokens` in `GET /health` counts these tokens across all
  transactions, because a single transaction's overrun is only visible to
  whoever reads that transaction — and nobody watches those.

During the settling window the transaction is still reported as `dispensing`:
the motor is off, but the count is not final yet, and a `POST` for another
`tx_id` in that window is the usual `409 busy`. There is no separate
`settling` state — a state a client would have to learn to ignore.

The opposite error has the same cause and is invisible in the other direction:
the coin line is an optocoupler output next to a motor, and a bouncing sensor
or an EMI spike produces falling edges that are not coins. The device accepts
an edge as a token only if at least 20 ms have passed since the last accepted
one (the hopper's own pulse is 30–65 ms, and coins arrive about a second
apart), and reports the rejected edges as `metrics.filtered_pulses`. Counting
raw edges ended the dispense early, so the member got fewer tokens than the
terminal billed.

### 5. Dispense-First, Pay-After
Tokens are **physically dispensed before payment processing**. This ensures:
- Exact token count tracking (even during failures)
- No payment refunds for dispense failures
- Simple reconciliation (what was dispensed = what is charged)

### 6. A fault is a device condition, and only a power cycle ends it

Two things used to be the same word. "The last transaction failed" and "this
machine needs a human" are different facts with different consequences, and
`error` meant both.

The device therefore carries a **fault** of its own, reported by `GET /health`
and independent of any transaction:

| `fault` | Raised by | Means |
|---------|-----------|-------|
| `none` | — | the device is fine |
| `jam` | the 5 s jam watchdog | nothing came out; something is wedged, or the hopper is empty |
| `hopper_error` | a decoded error on the hopper's error line (`fault_code` 1-7) | the hopper itself reported a fault |

Three rules, all of them owner decisions of 2026-09-20:

1. **A fault aborts the dispense at once.** The motor stops when the hopper
   says "motor fault", not five seconds later when the jam watchdog notices.
2. **While `fault != none`, a new `POST /dispense` is `409 fault`.** An
   idempotent hit — the transaction that is running, or one in the history
   ring — is still answered with `200` and its state: that caller is asking
   what happened to tokens that already fell, not asking for new ones. `GET
   /dispense/{tx_id}` likewise keeps answering.
3. **A fault is cleared by a reboot and by nothing else.** There is no
   `POST /reset`, no button in the TUI and none on the kiosk. The staff
   instruction is *clear the jam, refill if empty, pull the plug for 5 s*
   (`hardware/README.md`).

The fault is **not persisted**: any boot clears it, exactly as before. That a
watchdog reset, the RST button or the WiFi supervisor's restart also clears a
jam is known and accepted — if the jam is still there, the next dispense runs
into the 5 s timeout and faults again, having dispensed and billed nothing.

**A recovered crash sets no fault.** The transaction is an `error`; the device
is `idle`. This is the whole point of the separation: one watchdog reset used
to take the machine out of service, because the terminal read `error`, greyed
the token products out, and the dispense that would have cleared the state was
exactly the one nobody could start.

There is no self-healing any more. A successful dispense used to clear an
active hardware error, which cannot happen now — a decoded error faults the
device, and a faulted device does not dispense.

### 7. No sensor that is not fitted

`hopper_low` is **not part of this protocol**. The Hopper U-II's empty sensor
is a factory option (`docs/azkoyen-hopper-protocol.md` § 1, *With Empty Sensor
(Optional)*) and the unit in the boathouse does not have it: D6 sat on its
pull-up and read *not low* forever, and `/health` published that as if it were
a measurement. A field that always says *fine* is worse than no field — every
consumer built on it (kiosk warning, admin panel, a `degraded` status) would
have been permanently and silently wrong.

An empty hopper therefore ends as a `jam`, and the early warning comes from
counting what was sold since the last refill, not from the device.

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
| `fault` | enum | Device fault: `"none"`, `"jam"`, `"hopper_error"` | `"jam"` |
| `error_type` | enum | Why a transaction failed: `"NONE"`, `"JAM_TIMEOUT"`, `"RESET"`, or an Azkoyen name | `"JAM_TIMEOUT"` |

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
  "state": "idle",
  "fault": "none",
  "fault_code": 0,
  "uptime": 84230,
  "firmware": "1.3.0",
  "heap_free": 27512,
  "reset_reason": "Power on",
  "wifi": {
    "rssi": -47,
    "ip": "192.168.188.243",
    "ssid": "Ponyhof",
    "reconnects": 2
  },
  "metrics": {
    "total_dispenses": 1247,
    "successful": 1189,
    "jams": 3,
    "partial": 2,
    "crashes": 1,
    "failures": 55,
    "requested_tokens": 3120,
    "dispensed_tokens": 3098,
    "overrun_tokens": 4,
    "filtered_pulses": 11
  },
  "error_history": []
}
```

**Response (200 OK) — faulted:**
```json
{
  "protocol": 2,
  "state": "fault",
  "fault": "hopper_error",
  "fault_code": 3,
  "uptime": 84230,
  "firmware": "1.3.0",
  "heap_free": 27512,
  "reset_reason": "Power on",
  "wifi": {"rssi": -47, "ip": "192.168.188.243", "ssid": "Ponyhof", "reconnects": 2},
  "metrics": {"...": "..."},
  "error_history": [
    {"code": 3, "type": "JAM_PERMANENT", "timestamp": 82150},
    {"code": 1, "type": "COIN_STUCK", "timestamp": 75400}
  ]
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `protocol` | integer | Protocol version; always `2`. A client that reads anything else refuses the device. |
| `state` | string | **Required.** The device: `"idle"`, `"dispensing"` or `"fault"`. One field, not two. |
| `fault` | string | **Required.** `"none"`, `"jam"` or `"hopper_error"` (Design Principle 6). |
| `fault_code` | integer | **Required.** The Azkoyen code 1-7 behind a `hopper_error`, `0` otherwise. |
| `uptime` | integer | Seconds since boot |
| `firmware` | string | Firmware version |
| `heap_free` | integer | **Required.** Free heap in bytes. A leak is invisible in one reading and fatal over a season; a soak run compares it against its own start. |
| `reset_reason` | string | **Required.** Why the device last booted, in the SDK's own words (`"Power on"`, `"External System"`, `"Software Watchdog"`, `"Exception"`, …). Free-form: a reader compares it against the previous value rather than parsing it. |
| `wifi` | object | WiFi connection info (`rssi`, `ip`, `ssid`, `reconnects`) |
| `wifi.reconnects` | integer | **Required.** Times the link came back since boot. A device reconnecting ten times a night has a problem RSSI alone never shows. |
| `metrics` | object | Dispense metrics |
| `metrics.total_dispenses` | integer | Total dispense attempts since boot |
| `metrics.successful` | integer | Completed successfully |
| `metrics.jams` | integer | Jam timeouts |
| `metrics.partial` | integer | Failed transactions that had already dispensed something |
| `metrics.crashes` | integer | Transactions recovered after a reset |
| `metrics.failures` | integer | Total failures (jams + other errors) |
| `metrics.requested_tokens` | integer | Tokens asked for, across all transactions |
| `metrics.dispensed_tokens` | integer | Tokens that actually left the hopper |
| `metrics.overrun_tokens` | integer | Tokens delivered past the requested quantity (Design Principle 4b) |
| `metrics.filtered_pulses` | integer | Falling edges on the coin line rejected as noise |
| `error_history` | array | Last 5 decoded hopper errors, newest first |
| `error_history[].code` | integer | Error code (1-7, see Error Codes) |
| `error_history[].type` | string | Error type name |
| `error_history[].timestamp` | integer | Uptime when the error was decoded |

**What is deliberately NOT here:**

- **`status`** (`ok`/`degraded`/`error`) — it overlapped `dispenser`, the
  terminal ORed the two together, and nothing decided which won when they
  disagreed. The firmware hard-coded it to `"ok"`, so it never said anything
  at all. `state` and `fault` replace both.
- **`dispenser`** — replaced by `state`, which is the same information with a
  `fault` value the old enum did not have.
- **the `error` block** — what an active hardware error *means* is the fault;
  the decoded errors themselves are in `error_history`, which has no `cleared`
  flag any more because nothing clears them short of a power cycle.
- **the `gpio` block** — raw pin levels moved to `GET /debug`. A monitor
  cannot act on them and a member cannot read them.
- **`hopper_low`** — see Design Principle 7.

**Usage:**
Monitors poll this every 60 seconds:
- Available: `state != "fault"`
- Needs a human: `fault != "none"` — and the errand is `fault`/`fault_code`
- Success rate: `successful / total_dispenses`
- About to become a support call: `heap_free` falling between polls,
  `reset_reason` changing without anybody pulling a plug, `wifi.reconnects`
  climbing. None of the three is an alert on its own reading; each of them is
  one across a day.

**What a reader may NOT assume about `reset_reason`:** it is the device's own
wording, not an enum, and a firmware that runs on another chip will word it
differently. Compare it with the value from the previous poll; do not match it
against a list.

---

### GET /debug

The raw pin levels, for the bench and the TUI. Nothing here is a protocol
promise about behaviour; it is an instrument.

**Authentication:** Required (`X-API-Key` header)

**Response (200 OK):**
```json
{
  "gpio": {
    "coin_pulse": {"raw": 1, "active": false},
    "error_signal": {"raw": 1, "active": false}
  }
}
```

There is no `hopper_low` pin here either (Design Principle 7).

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
  "count_reliable": true,
  "error_code": 0,
  "error_type": "NONE"
}
```

**Response (200 OK) - Idempotent (already exists):**
```json
{
  "tx_id": "a3f8c012",
  "state": "done",
  "quantity": 3,
  "dispensed": 3,
  "count_reliable": true,
  "error_code": 0,
  "error_type": "NONE"
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

Returned when **another** transaction is currently `dispensing` —
`active_tx_id` names it, and it is never the `tx_id` of the request itself.

**Response (409 Conflict) - Faulted:**
```json
{
  "error": "fault",
  "fault": "jam",
  "fault_code": 0
}
```

The device needs a human (Design Principle 6). `fault` says which errand —
`jam` is "clear it and refill if it is empty", `hopper_error` with its
`fault_code` is the hopper's own verdict. The answer never says how to clear
it, because there is no way from here: the instruction is to pull the plug.

An idempotent hit is **not** refused this way: a `POST` for the transaction
that failed, or for one in the history ring, still answers `200` with its
state, and so does `GET /dispense/{tx_id}`.

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

4. **Busy:** If **another** transaction is dispensing:
   - Return `409 Conflict` with `{"error": "busy", ...}`
   - Client should retry after delay or check status

5. **Faulted:** If the device has a fault:
   - Return `409 Conflict` with `{"error": "fault", "fault": …, "fault_code": …}`
   - Retrying does not help and must not be automatic: somebody has to clear
     the jam and power-cycle the device

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
  "count_reliable": true,
  "error_code": 0,
  "error_type": "NONE"
}
```

**Response (200 OK) - Recovered after a power loss:**
```json
{
  "tx_id": "a3f8c012",
  "state": "error",
  "quantity": 5,
  "dispensed": 0,
  "count_reliable": false,
  "error_code": 0,
  "error_type": "RESET"
}
```

**Response (200 OK) - The hopper reported a fault during it:**
```json
{
  "tx_id": "a3f8c012",
  "state": "error",
  "quantity": 5,
  "dispensed": 1,
  "count_reliable": true,
  "error_code": 5,
  "error_type": "MOTOR_FAULT"
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `tx_id` | string | Transaction ID |
| `state` | string | Current state: `"dispensing"`, `"done"`, `"error"` |
| `quantity` | integer | Requested token count |
| `dispensed` | integer | Actual tokens dispensed so far. **May exceed `quantity`** (Design Principle 4b) |
| `count_reliable` | boolean | **Required.** `false` means `dispensed` is a lower bound (see Design Principle 4a) |
| `error_code` | integer | **Required.** The Azkoyen code 1-7 when the hopper reported one during this transaction, `0` otherwise |
| `error_type` | string | **Required.** `"NONE"` on a transaction that did not fail; `"JAM_TIMEOUT"`, `"RESET"`, or the Azkoyen name for `error_code` |

**Why the pair is required on every response, including the successful ones:**
one flat `"error"` made a jam, an empty hopper and a dead sensor the same row
on the terminal. A field that appears only on failures would leave a reader
defaulting it — and "nothing went wrong" and "the device never said" are
different answers.

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
   - Trigger: `dispensed >= quantity`, then 500 ms of settling
   - Actions: Stop the motor at once, keep counting for `DISPENSE_SETTLING_MS`,
     then persist the final state and add it to the history
   - Result: Successful completion, with `dispensed >= quantity` — a token that
     fell while the disc coasted is part of it (Design Principle 4b)
   - The state stays `dispensing` for the whole window; the device is busy

3. **dispensing → error:**
   - Trigger: the 5 s jam timeout, or a decoded hopper error, or a reset
     while the transaction was running
   - Actions: stop the motor, persist the partial count with its
     `error_type`, and — for the first two — raise the device `fault`
   - Result: the transaction is closed. The *device* is faulted for the first
     two and idle for the third

**The transaction state and the device state are two different things.** A
transaction ends in `error` and stays there; whether the *machine* is usable
afterwards is the `fault` in `GET /health`:

| What happened | transaction | `state` | `fault` |
|---------------|-------------|---------|---------|
| Jam timeout | `error`, `JAM_TIMEOUT` | `fault` | `jam` |
| Decoded hopper error | `error`, the Azkoyen name | `fault` | `hopper_error` (+ `fault_code`) |
| Reset mid-dispense | `error`, `RESET` | `idle` | `none` |

**Leaving a fault:** a power cycle, and nothing else (Design Principle 6).
On boot the device is `idle` again, whatever it was before.

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

### Soak runs

The table answers *does this device speak the protocol*. The soak answers the
question a single request never can — *does it still speak it after two
hundred dispenses*:

```sh
# Cycle A of the epic, at the bench, with the hopper simulator in fast mode ('f')
token-tui conformance --endpoint http://192.168.4.20 --api-key … \
  --target simulator --soak 200 --json report-A.json
```

`--soak N` dispenses one token N times, polls the running transaction every
`--soak-poll` (500 ms by default) and polls `/health` once per cycle. It
asserts five things, each of them a failure mode that hides from a single
request:

| Assertion | What it catches |
|-----------|-----------------|
| `soak_all_requests_succeeded` | the one-in-two-hundred failure the terminal's retry logic hides |
| `soak_heap_free_within_10_percent` | a leak |
| `soak_uptime_is_monotonic` | a reset in the middle of the run |
| `soak_reset_reason_unchanged` | and which kind of reset it was |
| `soak_post_latency_p95_below_300ms` | modem sleep, which shows up here and nowhere else |

It runs **instead of** the case table, not after it: the table ends with the
destructive fault cases, and a faulted device answers `409` to every dispense
after them. `--json` writes the numbers in a `soak` block beside the rows.
CI soaks the Go mock 20 times, which proves the runner; the numbers only mean
something against a real board.

The table lives in `dispenser-client-tui/conformance_cases.go`. Adding to it:

- **Assert the protocol, not an implementation.** If the firmware and the mock
  disagree, that is the finding — the case does not get weakened until both
  pass.
- A case that is known to fail against one target carries a `Note` naming the
  issue that will fix it, so a red line reads as *known* or *new* at a glance.
- The overrun of Design Principle 4b is covered from both sides: the mock runs
  `overrun_is_reported_above_quantity` (its quantity-18 scenario) and
  `health_reports_overrun_tokens` in CI, and on a real device the hopper
  simulator's `b` and `c` commands drive `bounce_burst_counts_one_token_per_coin`
  and `coast_pulse_is_counted_as_an_overrun`, which need `--interactive`.
- The operational telemetry of issue #7 is one case,
  `health_reports_ops_telemetry`: `heap_free`, `reset_reason` and
  `wifi.reconnects` are required fields, and the suite's own fake device has a
  knob (`omitOpsTelemetry`) that proves the case can fail.
- No case is known-red today. `post_while_error_is_409` was, against the
  firmware, from #1 until #6 fixed it. It is now `post_while_fault_is_409`,
  green in CI against the mock and the suite's own fake device; the firmware
  side of it — a faulted device refuses a new POST — is pinned by the native
  test `test_jam_sets_fault_and_blocks_new_dispense`, and the case itself runs
  against the real thing with `--target simulator` on the bench.
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
| `409` | `fault` | The device has a jam or a hopper error | Clear the jam, refill if empty, power cycle |
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
- A decoded error aborts the running dispense and raises the device `fault`;
  only a power cycle ends it (Design Principle 6)
- `error_history` keeps the last 5 with their timestamps — there is no
  `cleared` flag, because nothing clears them

---

## Timing & Timeouts

### Hardware Timing

| Parameter | Value | Description |
|-----------|-------|-------------|
| Token dispense rate | ~2.5 seconds/token | Azkoyen Hopper U-II mechanical speed |
| Pulse width | 30ms | Opto-sensor pulse duration per token (30–65 ms per the datasheet) |
| Pulse detection | FALLING edge | GPIO interrupt trigger |
| Minimum pulse spacing | 20ms | Below the shortest legal pulse: an edge closer than this to the last accepted one is noise, not a coin |

### Software Timeouts

| Timeout | Value | Description |
|---------|-------|-------------|
| Jam detection | 5 seconds | No pulse received → jam error |
| Settling window | 500 ms | After the target count: the motor is off, the count is still running (Design Principle 4b) |
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
  "dispensed": 2,
  "count_reliable": true,
  "error_code": 0,
  "error_type": "JAM_TIMEOUT"
}
```

**Resolution:**
1. Operator physically clears the jam — and refills the hopper if it was
   simply empty, which looks exactly the same from outside
2. Power cycle the ESP8266 (5 s off)
3. On boot the fault is gone and the device is `idle`
4. Client records the partial dispense in its local DB
5. Backend reconciliation handles refund/credit

While the fault is up, every new `POST /dispense` is `409 fault`; a retry of
the transaction that jammed still answers `200` with its partial count.

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
2. Polls `GET /health` to check `state`
3. When `state == "idle"`, retries original request

---

### Hardware Error During a Dispense

**Scenario:** the hopper reports a decoded error (e.g. MOTOR_FAULT, code 5)
while tokens are being dispensed.

**Detection:**
1. The error line is pulse-encoded: a 100 ms start pulse plus N×10 ms pulses
2. The firmware decodes it in `loop()` and hands it to the dispense manager —
   the one place that may stop a motor
3. The motor stops **now**, not in five seconds when the jam watchdog notices
4. The transaction is closed as `error` with `error_code: 5`,
   `error_type: "MOTOR_FAULT"` and the exact count of what did fall
5. The device raises `fault: "hopper_error"` with `fault_code: 5`

```bash
$ curl http://192.168.4.20/health
{"protocol":2,"state":"fault","fault":"hopper_error","fault_code":5,"...":"..."}

$ curl -X POST http://192.168.4.20/dispense -H "X-API-Key: secret" \
  -H "Content-Type: application/json" -d '{"tx_id":"next1","quantity":2}'
409 Conflict
{"error":"fault","fault":"hopper_error","fault_code":5}
```

**Resolution:** a human, then a power cycle. There is no self-healing: a
successful dispense used to clear an active hardware error, which cannot
happen any more, because a faulted device does not dispense.

---

### Reset Mid-Dispense Leaves the Device Usable

**Scenario:** a watchdog reset, an exception or a brownout while the motor
runs — the reset a motor on a shared supply actually causes.

**Behaviour:** the transaction is recovered and closed as `error` with
`error_type: "RESET"` and its count (Design Principle 4), and the device comes
up `idle` with `fault: "none"`.

**Why it matters:** before issue #6 the recovered transaction left the device
reporting `error`. The terminal treats an unavailable dispenser by greying out
the token products — so the only thing that could have cleared the state, a
new dispense, was the one thing nobody could start. Somebody had to pull the
plug on a machine with nothing wrong with it.

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
{"tx_id":"jam123","state":"error","quantity":5,"dispensed":2,"count_reliable":true,"error_code":0,"error_type":"JAM_TIMEOUT"}

# 4. Check health (the device is faulted)
$ curl http://192.168.4.20/health
{"state":"fault","fault":"jam","fault_code":0,...}

# 5. Operator clears the jam, refills if empty, power cycles the ESP8266

# 6. After the reboot the device is idle again
$ curl http://192.168.4.20/health
{"state":"idle","fault":"none","fault_code":0,...}

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
  - D6 (GPIO12): free — the empty sensor is not fitted (Design Principle 7)

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
  uint8_t error_kind;      // why it failed: none | jam_timeout | hopper | reset
  uint8_t error_code;      // the Azkoyen code 1-7 for a hopper error, else 0
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

The **device fault is deliberately not in this record.** A fault is cleared by
any boot, which is the whole of the reset story; persisting it would make the
power cycle that is supposed to end a jam the one thing that cannot.

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
- **The fault model (#6):** the device carries a `fault` (`none | jam |
  hopper_error` with a `fault_code`) of its own, separate from "the last
  transaction failed". A jam timeout or a decoded hopper error raises it and
  aborts the dispense at once; while it is up a new `POST /dispense` is
  `409 fault` and an idempotent hit is still `200`. It is cleared by a reboot
  and by nothing else — no reset route, none in the TUI, none on the kiosk —
  and it is **not persisted**, so any boot clears it. A recovered crash sets
  **no** fault: the transaction is an `error`, the device is `idle` and
  sellable. `GET /health` was redesigned around one `state` and one `fault`;
  `status`, `dispenser`, the `error` block and the raw `gpio` block are gone,
  and the pin levels moved to `GET /debug`. `hopper_low` was **removed from
  the protocol**: the empty sensor is a factory option this hopper does not
  have. Transaction responses now carry required `error_code` / `error_type`.
  Persisted layout version 3.

### Version 2.1.0 (2026-09-21) — still protocol 2
- **Operational telemetry (#7):** `GET /health` gains `heap_free`,
  `reset_reason` and `wifi.reconnects`, all three required. A device that
  reset once a week used to leave exactly one trace — a small `uptime` — and
  nothing said whether it was a watchdog, an exception or a power cut.
  Additive, so the protocol version stays 2.
- **WiFi is supervised (#7):** modem sleep is off
  (`WIFI_NONE_SLEEP`) — on an ESP8266 answering HTTP it is the usual cause of
  a request that takes seconds or times out once and succeeds on retry.
  A link that is down for longer than 60 s **and** no dispense running
  restarts the device; never while the motor is on. A failed join at boot is
  no longer final. **A restart clears the device fault, exactly as any boot
  does** — that is unchanged by design (a jam that is still there faults the
  next dispense again, having dispensed and billed nothing).
- **Soak runs (#7):** `token-tui conformance --soak N`, see *Conformance*.

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
