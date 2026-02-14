# Error Signal Wiring Guide

This guide shows how to connect the Azkoyen Hopper error signal (Pin 8) to the ESP8266 for error decoding.

## Overview

The error signal uses **pulse-encoded error codes** - a 100ms start pulse followed by 1-7 code pulses (10ms each). The ESP8266 decodes these pulses using interrupt-driven state machine on D5 (GPIO14).

## Signal Characteristics

**Hopper Pin 8 (Error Signal):**
- **At rest (no error):** HIGH level (~12V)
- **Error active:** LOW (0V)
- **Output type:** Open collector transistor
- **Error format:** 100ms start pulse + N×10ms code pulses (N = 1-7)

## Wiring Diagram

```
Azkoyen Hopper                    PC817 Optocoupler #3              ESP8266 (Wemos D1 Mini)
┌─────────────┐                   ┌──────────────┐                 ┌──────────────┐
│             │                   │              │                 │              │
│  Pin 8 ─────┼───────────────────┼──> IN+       │                 │              │
│  (Error)    │                   │              │                 │              │
│             │                   │   PC817      │                 │              │
│  Pin 4,5,6 ─┼───────┬───────────┼──> IN-       │                 │              │
│  (GND)      │       │           │              │                 │              │
│             │       │           │   OUT- ──────┼─────────────────┼──> D5 (GPIO14)
└─────────────┘       │           │              │                 │              │
                      │           │   OUT+ ──────┼─────┬───────────┼──> 3.3V
                      │           │              │     │           │              │
                      │           │   VCC ───────┼─────┤           │              │
                      │           │              │     │           │              │
                      │           └──────────────┘     │           └──────────────┘
                      │                                │
                      └────────────────────────────────┘
                                    Common GND
```

## Step-by-Step Wiring

### PC817 Optocoupler Module #3

Most PC817 modules have 4 terminals:

```
┌──────────────┐
│   IN+   IN-  │  Input side (hopper)
│              │
│   VCC   GND  │  Not used (PC817 is passive)
│              │
│   OUT+ OUT-  │  Output side (ESP8266)
└──────────────┘
```

**⚠️ Important:** Some modules label pins differently. Identify the LED side (IN) vs transistor side (OUT) using the component markings.

### Input Side (Hopper → Optocoupler)

1. **Hopper Pin 8 → Optocoupler IN+**
   - Connect hopper error signal (Pin 8) to optocoupler IN+
   - This is the signal wire

2. **Hopper GND → Optocoupler IN-**
   - Connect hopper ground (Pins 4, 5, or 6) to optocoupler IN-
   - All three GND pins are connected internally

**Verification:** When error occurs, LED on optocoupler should blink (100ms + short pulses)

### Output Side (Optocoupler → ESP8266)

3. **Optocoupler OUT+ → ESP8266 3.3V**
   - Provides pull-up voltage for open collector output
   - **Must be 3.3V** (NOT 5V - would damage ESP8266!)

4. **Optocoupler OUT- → ESP8266 D5 (GPIO14)**
   - This is the signal to the microcontroller
   - Configured as INPUT_PULLUP in firmware

5. **Common Ground**
   - Connect hopper GND to ESP8266 GND
   - Required for proper signal reference

## Signal Logic

**How it works:**

1. **No error (rest state):**
   - Hopper Pin 8: HIGH (~12V)
   - Optocoupler LED: OFF
   - Optocoupler OUT-: HIGH (pulled up to 3.3V)
   - ESP8266 D5 reads: HIGH

2. **Error pulse (100ms start + 10ms codes):**
   - Hopper Pin 8: LOW (0V)
   - Optocoupler LED: ON
   - Optocoupler OUT-: LOW (~0V, transistor conducting)
   - ESP8266 D5 reads: LOW

**Signal is NOT inverted** - LOW on hopper Pin 8 = LOW on ESP8266 D5.

## Firmware Configuration

The firmware is already configured for this:

```cpp
// In config.h
#define ERROR_SIGNAL_PIN   D5    // GPIO14

// In hopper_control.cpp
pinMode(ERROR_SIGNAL_PIN, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(ERROR_SIGNAL_PIN),
                handleErrorPinChange, CHANGE);
```

## Testing Without Hopper

To test error decoding without the hopper:

### Manual Test Setup

1. Connect a jumper wire from ESP8266 D5 to GND through a button/switch
2. Press and hold for timing:
   - **100ms** (start pulse)
   - Release for 10ms
   - **10ms** press × N times (error code)
   - Release

### Arduino Test Sketch

You can create a test signal generator:

```cpp
// On a second Arduino/ESP8266
void simulateError(int errorCode) {
  digitalWrite(TEST_PIN, LOW);  // 100ms start pulse
  delay(100);
  digitalWrite(TEST_PIN, HIGH);
  delay(20);

  for (int i = 0; i < errorCode; i++) {
    digitalWrite(TEST_PIN, LOW);  // 10ms code pulse
    delay(10);
    digitalWrite(TEST_PIN, HIGH);
    delay(10);
  }
}
```

Connect TEST_PIN to D5 on the dispenser ESP8266.

## Verification

### Serial Monitor Output

When error detected, you should see:

```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
[ErrorHistory] Added error: JAM_PERMANENT at timestamp 12345
```

### HTTP Endpoint

```bash
curl http://192.168.4.20/health | jq '.error'
```

Expected response:
```json
{
  "active": true,
  "code": 3,
  "type": "JAM_PERMANENT",
  "timestamp": 12345,
  "description": "Permanent jam detected"
}
```

## Troubleshooting

### No Errors Detected

1. **Check optocoupler LED:** Should blink when error occurs
   - If no blink: Check hopper Pin 8 connection
   - If blinks but no detection: Check output side wiring

2. **Check voltage at D5:**
   - Use multimeter
   - Should be ~3.3V at rest, ~0V during error pulse
   - If stuck HIGH: Check OUT+ to 3.3V connection
   - If stuck LOW: Check for short

3. **Verify firmware:**
   - Serial monitor should show: `[HopperControl] Interrupt attached to ERROR_SIGNAL_PIN (CHANGE edge)`
   - If not: Re-upload firmware

### Wrong Error Codes Detected

1. **Check pulse timing:**
   - Start pulse should be 90-110ms (100ms ±10%)
   - Code pulses should be 8-12ms (10ms ±20%)
   - Use oscilloscope or logic analyzer to verify

2. **Electrical noise:**
   - Add 0.1µF capacitor between D5 and GND (close to ESP8266)
   - Use twisted pair or shielded cable for long runs

### Optocoupler Always ON

- Check polarity: IN+ should go to hopper Pin 8, IN- to GND
- Verify hopper ground connection
- Test optocoupler with multimeter (LED forward voltage ~1.2V)

## Safety Notes

- **Never connect 12V directly to ESP8266!** Always use optocoupler isolation
- **Use 3.3V for OUT+ pull-up**, not 5V
- **Common ground required** between hopper and ESP8266
- **Optocoupler must be rated for 12V input** (PC817 is rated for 30V)

## Parts List

- **PC817 Optocoupler Module** (1x for error signal)
  - Amazon: Search "PC817 optocoupler module 4 channel"
  - ~$5-10 for 4-channel board
  - Already have 4-channel? Use channel #3

- **Jumper Wires** (Female-Female recommended)
  - For prototyping connections
  - Eventually solder for production

- **Optional: 0.1µF Capacitor**
  - Noise filtering if needed
  - Ceramic or film type

## Complete 4-Signal Wiring

If wiring all signals (motor control, coin pulse, error, empty):

| Hopper Pin | Signal | Optocoupler | ESP8266 Pin |
|------------|--------|-------------|-------------|
| Pin 7 | Motor Control | #1 OUT- | D1 (GPIO5) |
| Pin 9 | Coin Pulse | #2 OUT- | D7 (GPIO13) |
| **Pin 8** | **Error Signal** | **#3 OUT-** | **D5 (GPIO14)** |
| Pin 10 | Empty Sensor | #4 OUT- | D6 (GPIO12) |
| Pins 4,5,6 | GND | All IN- | Common GND |

All optocoupler OUT+ connect to ESP8266 3.3V.

## References

- **Protocol Spec:** `docs/azkoyen-hopper-protocol.md` section 3.5
- **Error Codes:** `docs/azkoyen-hopper-protocol.md` section 4
- **Hardware Setup:** `docs/troubleshooting/motor-control-issues.md`
- **Optocoupler Details:** PC817 datasheet (bundled with module)
