# Motor Control Troubleshooting Guide

**This guide addresses the most common and time-consuming motor control issues.**

These problems took ~4 hours to debug initially. Use this guide to solve them in minutes.

---

## Issue 1: Motor Doesn't Engage During Dispense

### Symptom

- Send dispense request via HTTP API
- LED on optocoupler turns ON
- Motor does NOT start spinning
- After 5-second timeout, state goes to `error`
- Motor STILL doesn't run

### Root Cause

Hopper DIP switch is set to POSITIVE mode instead of NEGATIVE mode.

### How to Verify

**Measure hopper control pin (7) during dispense:**
```bash
# Trigger dispense
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: your-secret-api-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":1}'

# While dispensing (LED ON), measure:
# - Hopper pin 7 to GND with multimeter
# - Should be LOW (< 0.5V) if NEGATIVE mode
# - If HIGH (~6V or more), hopper is in POSITIVE mode
```

### Solution

1. Power off hopper and ESP8266
2. Open hopper (remove 4 screws on top panel)
3. Locate DIP switches near motor/circuit board
4. Find switch labeled "NEGATIVE/POSITIVE" or "POLARITY"
5. **Set to NEGATIVE position**
6. Close hopper and power back on
7. Test dispense again

**Expected behavior after fix:**
- Dispense request → LED turns ON → Motor immediately engages
- Timeout or completion → LED turns OFF → Motor stops

---

## Issue 2: Motor Engages AFTER Timeout (Inverted Behavior)

### Symptom

- Send dispense request
- LED turns ON
- Motor does NOT engage
- After timeout (~5 seconds):
  - LED turns OFF
  - Motor NOW starts running
  - Motor keeps running continuously

### Root Cause

**Same as Issue 1:** Hopper in POSITIVE mode (active HIGH) instead of NEGATIVE mode (active LOW).

With POSITIVE mode:
- Control pin HIGH → Motor ON (backwards!)
- Control pin LOW → Motor OFF

So when LED is ON (OUT low), motor is off. When LED goes OFF (OUT high), motor turns on.

### Solution

Same as Issue 1 - set hopper DIP switch to NEGATIVE mode.

---

## Issue 3: Motor Control Unreliable (Sometimes Works, Sometimes Doesn't)

### Symptom

- Motor behavior inconsistent
- Sometimes engages during dispense, sometimes doesn't
- Measured voltages seem wrong:
  - LED ON: OUT = 3V to 7V (should be near 0V)
  - LED OFF: OUT = 2V to 6V (should be 10-12V)

### Root Cause

PC817 optocoupler input current too low - stock R1 resistor (1kΩ) doesn't provide enough current for saturation.

**Technical explanation:**
- Stock R1 = 1kΩ → Input current = 3.3V / 1000Ω = **3.3mA**
- PC817 needs 10-20mA for proper saturation
- With only 3.3mA: phototransistor weakly conducting → OUT doesn't pull all the way to GND
- Result: OUT voltage in middle zone (3-7V) → unreliable motor control

### How to Verify

**Measure optocoupler voltages:**

```bash
# While LED is ON (during dispense):
# Measure optocoupler OUT pin to GND
#
# If > 1V: Input current too low (R1 too high)
# If < 0.5V: Input current good ✓
```

**Measure R1 resistance:**
```bash
# Power off circuit
# Measure resistance between optocoupler IN+ and IN-
#
# If ~1000Ω: Stock resistor, needs modification
# If ~200-300Ω: Modified correctly ✓
```

### Solution

**Add 330Ω resistor in parallel with R1 on motor control optocoupler (channel #1):**

1. Locate optocoupler module connected to D1 (motor control)
2. Find R1 resistor on module (labeled, usually near input side)
3. Solder 330Ω resistor across R1's two connection points:
   ```
   Module PCB:
       Point A ────/\/\/──── Point B
                   1kΩ (existing R1)

   Add 330Ω resistor:
       Point A ────/\/\/──── Point B
                   330Ω (new, in parallel)

   Result: (1kΩ || 330Ω) = 248Ω total
   ```

4. Verify modification:
   - Power off
   - Measure IN+ to IN- resistance = should be ~250Ω (not 1kΩ)
   - Power on
   - Trigger dispense
   - Measure OUT to GND while LED ON = should be < 0.5V

**Expected voltages after modification:**
- LED ON: OUT = **0.1-0.2V** (reliable LOW for motor ON)
- LED OFF: OUT = **~6V** (reliable HIGH for motor OFF with NEGATIVE mode)

**Why 6V is acceptable for HIGH:**
- Hopper input has ~10kΩ impedance to ground
- Creates voltage divider with R2 (10kΩ pull-up)
- 12V × (10kΩ / 20kΩ) = 6V
- NEGATIVE mode threshold ~3-4V, so 6V registers as HIGH ✓

---

## Issue 4: Optocoupler LED Bright But Motor Doesn't Respond

### Symptom

- Optocoupler input LED is bright red (good current)
- OUT voltage still too high (> 1V) when LED ON
- Motor doesn't engage

### Root Cause

One of:
1. R1 parallel resistor not soldered correctly
2. Output side issue (R2 or wiring)
3. Defective optocoupler

### Diagnosis Steps

**Step 1: Verify R1 modification**
```bash
# Power off
# Measure resistance IN+ to IN-
# Should be ~250Ω
# If ~1kΩ: Parallel resistor not working, check solder joints
```

**Step 2: Test optocoupler without hopper load**
```bash
# Disconnect optocoupler OUT from hopper control pin
# Trigger dispense (LED ON)
# Measure OUT to GND
#
# Should be < 0.2V (nearly 0V)
# If > 1V even without load: Optocoupler defective or input current still too low
```

**Step 3: Verify VCC connected**
```bash
# Measure optocoupler VCC pin to GND
# Should be ~12V
# If 0V: VCC not connected (pull-up won't work)
```

**Step 4: Check for strong pull-up modifications**
```bash
# If you added extra parallel resistors to R2 (output pull-up), remove them
# R2 should be 10kΩ only (stock value is correct)
# Extra strong pull-up (< 1kΩ) can prevent saturation
```

### Solutions

- **R1 not modified correctly:** Resolder 330Ω parallel resistor, verify with multimeter
- **VCC not connected:** Connect VCC pin to hopper 12V supply
- **Defective optocoupler:** Replace with known-good PC817 module
- **Extra R2 modification:** Remove parallel resistors from R2, leave at 10kΩ

---

## Diagnostic Flowchart

```
Motor doesn't engage during dispense
  │
  ├─> LED doesn't turn ON
  │     └─> Check firmware, WiFi connection, HTTP API
  │
  └─> LED turns ON
        │
        ├─> Measure OUT to GND while LED ON
        │
        ├─> OUT > 1V (too high)
        │     └─> Issue 3: R1 too high
        │           → Add 330Ω parallel resistor
        │
        └─> OUT < 0.5V (good)
              │
              └─> Measure hopper pin 7 to GND
                    │
                    ├─> Same as OUT (< 0.5V)
                    │     └─> Check hopper DIP switch
                    │           → Issue 1: Set to NEGATIVE mode
                    │
                    └─> Different from OUT
                          └─> Wiring issue: OUT not connected to pin 7
```

---

## Quick Reference: Expected Voltage Levels

**All measurements to GND, hopper in NEGATIVE mode, R1 modified (330Ω parallel):**

| Location | LED OFF | LED ON | Notes |
|----------|---------|--------|-------|
| **D1 (ESP8266 GPIO)** | 0V | 3.3V | Firmware control |
| **Optocoupler OUT (no load)** | ~12V | < 0.2V | Ideal saturation |
| **Optocoupler OUT (with hopper)** | ~6V | < 0.5V | Voltage divider OK |
| **Hopper pin 7** | ~6V | < 0.5V | Should match OUT |
| **Motor behavior** | OFF | ON | Expected result |

**If voltages don't match this table, use diagnostics above.**

---

## Hardware Modification Checklist

Before attempting firmware troubleshooting, verify hardware:

- [ ] Hopper DIP switch set to **NEGATIVE** mode
- [ ] Optocoupler #1 (D1, motor control) R1 modified: 330Ω in parallel
  - [ ] Measured IN+ to IN-: ~250Ω (not 1kΩ)
- [ ] Optocoupler VCC pin connected to 12V
- [ ] Optocoupler OUT pin connected to hopper control pin (7)
- [ ] Optocoupler GND connected to hopper GND (4 or 6)
- [ ] 12V power supply connected and ON
- [ ] ESP8266 powered and WiFi connected

**If all checkboxes ✓ and motor still doesn't work: Check firmware/software issues.**

---

## Lessons Learned

**Time to debug without this guide:** ~4 hours

**Time to debug with this guide:** ~10 minutes

**Most common mistake:** Hopper in POSITIVE mode (Issue 1/2)

**Most time-consuming mistake:** Stock R1 resistor too high (Issue 3)

**Best diagnostic tool:** Multimeter measuring OUT voltage while LED ON
- < 0.5V → hardware good, check hopper mode
- > 1V → R1 needs modification

---

**Last Updated:** 2026-02-13
**Based on:** Extensive debugging session resolving motor control issues
