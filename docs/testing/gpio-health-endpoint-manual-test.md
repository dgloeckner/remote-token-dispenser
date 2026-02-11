# Manual Testing: GPIO Health Endpoint

**Feature:** Extended health endpoint with GPIO pin states
**Date:** 2026-02-11
**Requires:** ESP8266 hardware with firmware flashed

## Test Setup

1. Flash the updated firmware to ESP8266
2. Note the device IP address from Serial Monitor
3. Use `curl` or browser to test endpoints

## Test Cases

### Test 1: Health Endpoint Structure

**Purpose:** Verify the new JSON structure is correct

**Steps:**
```bash
curl http://192.168.4.20/health
```

**Expected Response:**
```json
{
  "status": "ok",
  "uptime": <number>,
  "firmware": "1.0.0",
  "dispenser": "idle",
  "gpio": {
    "coin_pulse": {
      "raw": 1,
      "active": false
    },
    "error_signal": {
      "raw": 1,
      "active": false
    },
    "hopper_low": {
      "raw": <0 or 1>,
      "active": <true if empty>
    }
  },
  "metrics": {
    "total_dispenses": 0,
    "successful": 0,
    "jams": 0,
    "partial": 0,
    "failures": 0
  }
}
```

**Verify:**
- [ ] Response has `gpio` object
- [ ] No top-level `hopper_low` field (breaking change confirmed)
- [ ] Each GPIO pin has `raw` and `active` fields
- [ ] `raw` values are 0 or 1
- [ ] `active` values are boolean

### Test 2: Hopper Disconnected (All Pins Inactive)

**Purpose:** Verify INPUT_PULLUP configuration - all pins should read HIGH when disconnected

**Setup:** Disconnect hopper from ESP8266 (or use bench power only)

**Steps:**
```bash
curl http://192.168.4.20/health | jq '.gpio'
```

**Expected:**
```json
{
  "coin_pulse": {
    "raw": 1,
    "active": false
  },
  "error_signal": {
    "raw": 1,
    "active": false
  },
  "hopper_low": {
    "raw": 1,
    "active": false
  }
}
```

**Verify:**
- [ ] All `raw` values are 1 (pulled HIGH)
- [ ] All `active` values are false (no signals detected)

### Test 3: Hopper Connected - Idle State

**Purpose:** Verify normal idle state readings

**Setup:** Connect hopper, ensure it has tokens, no dispense active

**Steps:**
```bash
curl http://192.168.4.20/health | jq '.gpio'
```

**Expected:**
```json
{
  "coin_pulse": {
    "raw": 1,
    "active": false
  },
  "error_signal": {
    "raw": 1,
    "active": false
  },
  "hopper_low": {
    "raw": 1,
    "active": false
  }
}
```

**Verify:**
- [ ] `coin_pulse`: inactive (no pulses when idle)
- [ ] `error_signal`: inactive (no errors)
- [ ] `hopper_low`: inactive (has tokens) - raw=1, active=false

### Test 4: During Active Dispensing

**Purpose:** Observe coin_pulse transitions during operation

**Setup:** Trigger a dispense operation

**Steps:**
```bash
# Start dispense
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: your-api-key" \
  -H "Content-Type: application/json" \
  -d '{"tx_id": "test123", "quantity": 3}'

# Immediately poll health (repeatedly during dispensing)
watch -n 0.5 'curl -s http://192.168.4.20/health | jq ".gpio.coin_pulse"'
```

**Expected:**
You should see `coin_pulse` flickering between states:
- Sometimes: `{"raw": 1, "active": false}` (between pulses)
- Sometimes: `{"raw": 0, "active": true}` (during pulse)

**Verify:**
- [ ] `coin_pulse.raw` alternates between 0 and 1 during dispensing
- [ ] `coin_pulse.active` alternates between true and false
- [ ] After dispensing completes, returns to `{"raw": 1, "active": false}`

### Test 5: Hopper Empty Condition

**Purpose:** Verify hopper_low sensor detection

**Setup:** Remove all tokens from hopper (or trigger low sensor mechanically)

**Steps:**
```bash
curl http://192.168.4.20/health | jq '.gpio.hopper_low'
```

**Expected:**
```json
{
  "raw": 0,
  "active": true
}
```

**Verify:**
- [ ] `hopper_low.raw` is 0 (LOW signal)
- [ ] `hopper_low.active` is true (sensor triggered)
- [ ] Refill hopper → returns to `{"raw": 1, "active": false}`

### Test 6: Error Signal (If Testable)

**Purpose:** Verify error_signal reading during hardware fault

**Setup:** Simulate jam or hardware error (if safe to do so)

**Steps:**
```bash
curl http://192.168.4.20/health | jq '.gpio.error_signal'
```

**Expected (if error active):**
```json
{
  "raw": 0,
  "active": true
}
```

**Verify:**
- [ ] `error_signal.raw` is 0 when error present
- [ ] `error_signal.active` is true when error present
- [ ] Returns to `{"raw": 1, "active": false}` when error clears

**Note:** This may not be easily testable without actual hardware fault. Skip if not applicable.

## Inverted Logic Verification

All pins use optocoupler-based inverted logic:
- **Physical signal present** → GPIO reads LOW → `raw: 0, active: true`
- **Physical signal absent** → GPIO reads HIGH (pulled up) → `raw: 1, active: false`

This should be consistent across all three pins.

## Breaking Change Verification

**Old client code:**
```javascript
const response = await fetch('http://192.168.4.20/health');
const data = await response.json();
const lowLevel = data.hopper_low; // ❌ This will be undefined now
```

**New client code:**
```javascript
const response = await fetch('http://192.168.4.20/health');
const data = await response.json();
const lowLevel = data.gpio.hopper_low.active; // ✅ Correct
```

If there's any Pi daemon or monitoring code, it must be updated.

## Test Completion Checklist

- [ ] Test 1: Structure verified
- [ ] Test 2: Disconnected state verified
- [ ] Test 3: Idle state verified
- [ ] Test 4: Coin pulse transitions observed
- [ ] Test 5: Hopper empty detection verified
- [ ] Test 6: Error signal tested (if applicable)
- [ ] Breaking change impact assessed
- [ ] Any dependent client code updated

## Notes

Record any observations, unexpected behavior, or issues here:

---
