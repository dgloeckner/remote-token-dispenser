# GPIO Pin Status in Health Endpoint

**Date:** 2026-02-11
**Status:** Approved

## Overview

Extend the `/health` endpoint to expose real-time GPIO pin states for all three input pins from the Azkoyen Hopper. This provides complete visibility into hardware signals for debugging and monitoring.

## Requirements

Expose the following GPIO pins in the health endpoint response:
- **coin_pulse** (D2/GPIO4) - Coin pulse input signal
- **error_signal** (D5/GPIO14) - Hopper error input signal
- **hopper_low** (D6/GPIO12) - Empty sensor input signal

Each pin should include:
- **raw** - Direct GPIO reading (0=LOW, 1=HIGH)
- **active** - Logical interpretation (true when signal is active)

## Response Structure

```json
{
  "status": "ok",
  "uptime": 12345,
  "firmware": "1.0.0",
  "dispenser": "idle",
  "gpio": {
    "coin_pulse": {"raw": 1, "active": false},
    "error_signal": {"raw": 1, "active": false},
    "hopper_low": {"raw": 0, "active": true}
  },
  "metrics": {...}
}
```

### Value Interpretation

- `raw`: 0 or 1 (direct GPIO reading)
  - 0 = LOW
  - 1 = HIGH
- `active`: boolean (true when signal is active)
  - All pins use INPUT_PULLUP with inverted logic
  - Active when raw = 0 (LOW = signal active)

## Breaking Change

**Previous structure:**
```json
{
  "hopper_low": true
}
```

**New structure:**
```json
{
  "gpio": {
    "hopper_low": {"raw": 0, "active": true}
  }
}
```

Any client code reading `hopper_low` must be updated to read `gpio.hopper_low.active`.

## Implementation Changes

### 1. HopperControl Class (`hopper_control.h`)

Add new public methods:

```cpp
// Coin pulse pin
uint8_t getCoinPulseRaw();      // Returns 0 or 1
bool isCoinPulseActive();       // Returns true if LOW

// Error signal pin
uint8_t getErrorSignalRaw();    // Returns 0 or 1
bool isErrorSignalActive();     // Returns true if LOW

// Hopper low pin
uint8_t getHopperLowRaw();      // Returns 0 or 1
// isHopperLow() already exists
```

### 2. HopperControl Implementation (`hopper_control.cpp`)

**Add pin initialization in `begin()`:**
```cpp
pinMode(ERROR_SIGNAL_PIN, INPUT_PULLUP);
```

**Implement new methods:**
```cpp
uint8_t HopperControl::getCoinPulseRaw() {
  return digitalRead(COIN_PULSE_PIN) == LOW ? 0 : 1;
}

bool HopperControl::isCoinPulseActive() {
  return digitalRead(COIN_PULSE_PIN) == LOW;
}

uint8_t HopperControl::getErrorSignalRaw() {
  return digitalRead(ERROR_SIGNAL_PIN) == LOW ? 0 : 1;
}

bool HopperControl::isErrorSignalActive() {
  return digitalRead(ERROR_SIGNAL_PIN) == LOW;
}

uint8_t HopperControl::getHopperLowRaw() {
  return digitalRead(HOPPER_LOW_PIN) == LOW ? 0 : 1;
}
```

### 3. HTTP Server (`http_server.cpp`)

**Modify `handleHealth()` method:**

Remove:
```cpp
doc["hopper_low"] = hopperControl.isHopperLow();
```

Add:
```cpp
JsonObject gpio = doc.createNestedObject("gpio");

JsonObject coinPulse = gpio.createNestedObject("coin_pulse");
coinPulse["raw"] = hopperControl.getCoinPulseRaw();
coinPulse["active"] = hopperControl.isCoinPulseActive();

JsonObject errorSignal = gpio.createNestedObject("error_signal");
errorSignal["raw"] = hopperControl.getErrorSignalRaw();
errorSignal["active"] = hopperControl.isErrorSignalActive();

JsonObject hopperLow = gpio.createNestedObject("hopper_low");
hopperLow["raw"] = hopperControl.getHopperLowRaw();
hopperLow["active"] = hopperControl.isHopperLow();
```

## Testing & Validation

### Manual Testing

**With hopper disconnected (all pins pulled HIGH):**
- All pins should show: `"raw": 1, "active": false`

**During normal operation:**
- `coin_pulse`: Rapid transitions during dispensing
- `error_signal`: Should stay `"raw": 1, "active": false` unless hardware fault
- `hopper_low`: `"raw": 0, "active": true` when empty

**Test scenarios:**
1. Call `/health` with hopper disconnected - verify all inactive
2. Trigger dispense, poll `/health` during operation - observe coin_pulse transitions
3. Empty hopper - verify hopper_low shows active
4. Simulate error condition (if possible) - verify error_signal

### Edge Cases

**Coin pulse instantaneous state:**
- The `coin_pulse` reading is the current pin state, not a counter
- During dispensing, you may catch it mid-pulse (active) or between pulses (inactive)
- Use the existing pulse counter (`getPulseCount()`) for actual coin count

**Breaking change impact:**
- Update any client code (Pi daemon, monitoring scripts) to use new path
- Old path: `response.hopper_low`
- New path: `response.gpio.hopper_low.active`

## Files Modified

1. `firmware/dispenser/hopper_control.h` - Add method declarations
2. `firmware/dispenser/hopper_control.cpp` - Implement pin reading methods, initialize ERROR_SIGNAL_PIN
3. `firmware/dispenser/http_server.cpp` - Modify health endpoint response structure
