# Hopper Configuration Critical Documentation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Document critical hopper configuration requirements (NEGATIVE mode, optocoupler resistor modifications) to prevent future debugging pain.

**Architecture:** Update all documentation files, code comments, and hardware guides to prominently feature the NEGATIVE mode requirement and optocoupler resistor specifications that took extensive debugging to discover.

**Tech Stack:** Markdown documentation, C++ code comments, SVG diagram annotations

**Critical Context:** This addresses issues that caused multiple hours of debugging:
1. Hopper DIP switch MUST be set to NEGATIVE mode (active LOW control per Azkoyen protocol)
2. PC817 optocoupler modules (bestep brand) have inadequate resistor values:
   - R1 (input) = 1kΩ → too high, needs 330Ω in parallel for 10-15mA drive current
   - R2 (output) = 10kΩ → too weak for reliable pull-up, causes voltage divider issues
3. Wiring is D1→IN+ (not inverted), so GPIO HIGH = LED ON = motor ON

**Protocol Reference:** Official Azkoyen specification now documented in `docs/azkoyen-hopper-protocol.md`
- NEGATIVE mode requirement (section 2.1)
- Voltage thresholds: < 0.5V = LOW (motor ON), > 4V = HIGH (motor OFF)
- Signal specifications and timing requirements

---

## Task 1: Update Hardware Setup Guide (Critical Warnings)

**Files:**
- Modify: `hardware/README.md`

**Step 1: Add critical warning banner at top of file**

After the title, add this prominent warning section:

```markdown
## ⚠️ CRITICAL CONFIGURATION REQUIREMENTS

**READ THIS FIRST - These settings are mandatory or the motor will not work correctly:**

### 1. Hopper DIP Switch: NEGATIVE Mode Required

The Azkoyen Hopper U-II **MUST** be configured in **NEGATIVE mode** (active LOW control).

**Official specification:** See `docs/azkoyen-hopper-protocol.md` section 2.1

- ✅ **NEGATIVE mode:** Control pin LOW (< 0.5V) = motor ON, control pin HIGH (> 4V) = motor OFF
- ❌ **POSITIVE mode:** Will cause inverted behavior - motor runs at wrong times

**DIP switch configuration:**
- Required setting: **STANDARD + NEGATIVE** (see protocol doc section 5 for DIP matrix)
- Connector #6 on hopper control board

**How to set:**
1. Open hopper (4 screws on top panel)
2. Locate DIP switches (connector #6 near motor/circuit board)
3. Set to STANDARD + NEGATIVE configuration per protocol diagram
4. Close hopper and test

**Symptoms of wrong mode:**
- Motor doesn't engage during dispense request
- Motor engages AFTER timeout/error instead of during dispense
- Backwards behavior compared to LED indicator

**Reference:** Azkoyen protocol section 2.1 (NEGATIVE Logic Mode)

---

### 2. PC817 Optocoupler Resistor Modifications Required

The bestep brand PC817 optocoupler modules ship with **inadequate resistor values** that prevent proper motor control.

**Stock resistor values (DON'T WORK):**
- R1 (input) = 1kΩ → Provides only 3.3mA drive current (too weak!)
- R2 (output) = 10kΩ → Creates voltage divider with hopper input (unreliable voltages)

**Required modifications:**

| Resistor | Location | Stock Value | Add in Parallel | Result | Purpose |
|----------|----------|-------------|-----------------|--------|---------|
| R1 | Input side | 1kΩ | 330Ω | 248Ω total | 13.3mA drive current for saturation |
| R2 | Output side | 10kΩ | *(remove)* | 10kΩ | Weak pull-up works with modification |

**Why this matters:**
- Without R1 modification: Phototransistor won't saturate, OUT stays at ~3-7V instead of 0V
- With weak R2: Hopper input impedance creates voltage divider (6V instead of 12V)
- Result: Unreliable motor control, inconsistent operation

**How to modify:**
1. Locate R1 on optocoupler module #1 (motor control)
2. Solder 330Ω resistor in parallel across R1's two connection points
3. For R2: Remove any parallel resistors added during testing, leave at 10kΩ
4. Verify: Input current should be 10-15mA, output should saturate to < 0.2V

**Expected voltages after modification (motor control optocoupler #1):**
- LED OFF: OUT = ~6V (above 4V threshold, motor OFF per NEGATIVE mode)
- LED ON: OUT = ~0.1V (below 0.5V threshold, motor ON per NEGATIVE mode)

**Voltage thresholds (from Azkoyen protocol section 2.1):**
- Motor ON: Control pin < 0.5V
- Motor OFF: Control pin 4V to Vcc ±10% (typically 4-13.2V for 12V supply)

---
```

**Step 2: Update "Electronic Components" section**

Replace the existing PC817 components table with:

```markdown
### Electronic Components

**Required components:**

| Component | Quantity | Purpose | Notes |
|-----------|----------|---------|-------|
| PC817 optocoupler modules (bestep brand) | 4 | Galvanic isolation | ⚠️ Requires resistor modification! |
| 330Ω resistor (1/4W) | 1 | R1 modification for motor control optocoupler | Solder in parallel with stock 1kΩ |
| 2200µF 25V capacitor | 1 | Motor startup surge protection | Observe polarity! |

**⚠️ PC817 Module Resistor Modification:**

The stock PC817 modules include onboard resistors R1 (1kΩ) and R2 (10kΩ), but R1 is too high for reliable operation.

**You MUST modify R1 on the motor control optocoupler (channel #1):**
- Add 330Ω resistor in parallel with existing 1kΩ R1
- This provides proper drive current (13.3mA) for phototransistor saturation
- Without this: motor control will be unreliable or non-functional

**Coin pulse optocouplers (channels #2, #3, #4) can use stock resistors** - they don't need modification for input sensing.
```

**Step 3: Update Assembly Instructions - Step 1**

Find the "Step 1: Configure the Hopper" section and replace with:

```markdown
### Step 1: Configure the Hopper

The Azkoyen Hopper U-II has DIP switches that MUST be configured correctly.

**Required settings:**

1. **Control Mode: NEGATIVE (CRITICAL!)**
   - Switch labeled "NEGATIVE/POSITIVE" or "POLARITY" → set to **NEGATIVE**
   - This makes control pin active LOW (LOW = motor ON)
   - Wrong setting causes inverted motor behavior

2. **Coin Detection Mode: PULSES**
   - Switch for coin sensing mode → set to **PULSES**
   - Provides 30ms pulse per coin dispensed
   - Refer to [Azkoyen U-II manual](https://www.casino-software.de/download/hopper-azkoyen-u2-manual.pdf) for switch positions

**How to configure:**
1. Open hopper (4 screws on top panel)
2. Locate DIP switches inside (usually near motor)
3. Set switches per table above
4. Test manually: hopper should click 30ms per coin
5. Close hopper

**⚠️ WARNING:** If motor engages at wrong times or doesn't engage during dispense, check NEGATIVE mode setting first!
```

**Step 4: Add new troubleshooting section for this issue**

Before the "Additional Resources" section, add:

```markdown
---

## 🔧 Troubleshooting: Motor Control Issues

### Motor doesn't engage during dispense (or engages at wrong time)

**Symptom:** Motor starts AFTER dispense timeout instead of during dispense request, or never engages.

**Root cause:** Hopper DIP switch set to POSITIVE mode instead of NEGATIVE mode.

**Fix:**
1. Open hopper and check DIP switch labeled "NEGATIVE/POSITIVE"
2. Set to **NEGATIVE** position
3. Test: motor should engage immediately when dispense is triggered

**How to verify:**
- Measure hopper control pin (7) during dispense:
  - Should go LOW (~0V) when motor should run
  - Should stay HIGH (~6-12V) when motor should be off
- If inverted (HIGH when should run), hopper is in POSITIVE mode

---

### Motor control unreliable / inconsistent operation

**Symptoms:**
- Motor sometimes engages, sometimes doesn't
- Measured voltages on optocoupler OUT are wrong:
  - LED ON: OUT = 3-7V (should be < 0.5V)
  - LED OFF: OUT = 2-6V (should be 10-12V without hopper, 6V with hopper)

**Root cause:** PC817 optocoupler R1 (input resistor) is too high (1kΩ stock value).

**Fix:**
1. Locate R1 on motor control optocoupler module (channel #1)
2. Solder 330Ω resistor in parallel with R1
3. Verify with multimeter: resistance between IN+ and IN- should be ~250Ω

**Expected voltages after fix:**
- LED ON: OUT < 0.2V (motor engages reliably)
- LED OFF: OUT ~6V with hopper connected (motor off reliably)

**Technical explanation:**
- Stock R1 = 1kΩ provides only 3.3mA input current (3.3V / 1kΩ)
- PC817 needs 10-20mA for proper saturation
- Adding 330Ω in parallel: (1kΩ || 330Ω) = 248Ω → 13.3mA ✓

---

### Optocoupler LED is bright but motor doesn't respond

**Symptom:** Input LED on optocoupler module is bright red, but OUT voltage doesn't drop low enough.

**Root cause:** Input current is good, but output pull-up (R2) may be too strong, or hopper input impedance is loading the output.

**Fix:**
1. Verify R1 modification is correct (330Ω in parallel)
2. Remove any extra parallel resistors from R2 (should be 10kΩ only)
3. Measure OUT to GND with hopper connected:
   - LED ON: Should be < 0.5V
   - LED OFF: Will be ~6V (this is normal with hopper's input impedance)

**Why 6V is OK for HIGH:**
- Hopper input has ~10kΩ impedance to ground
- Creates voltage divider with R2 (10kΩ pull-up): 12V × (10kΩ / 20kΩ) = 6V
- NEGATIVE mode threshold: > 4V = HIGH (per protocol section 2.1), so 6V is well above ✓
- Official spec: 4V to Vcc ±10% qualifies as HIGH

**Reference:** `docs/azkoyen-hopper-protocol.md` section 2.1 for voltage thresholds

---
```

**Step 5: Commit hardware README updates**

```bash
git add hardware/README.md
git commit -m "docs(hardware): add critical warnings for NEGATIVE mode and optocoupler resistor modifications

- Add prominent warning banner about NEGATIVE mode requirement
- Document PC817 resistor modification requirements (R1 needs 330Ω parallel)
- Add troubleshooting section for motor control issues
- Explain voltage divider behavior with hopper input impedance

These findings prevented hours of debugging and are critical for setup."
```

---

## Task 2: Update Firmware Documentation

**Files:**
- Modify: `firmware/README.md`

**Step 1: Add critical configuration section**

After the "Quick Start" section, add:

```markdown
---

## ⚠️ Critical Hardware Configuration

**Before flashing firmware, verify hardware is configured correctly:**

### 1. Hopper DIP Switch: NEGATIVE Mode

The Azkoyen Hopper U-II **MUST** be in NEGATIVE mode:
- Control signal: active LOW (< 0.5V = motor ON, > 4V = motor OFF)
- DIP switch: STANDARD + NEGATIVE configuration
- If in POSITIVE mode: motor behavior will be inverted
- **Protocol reference:** `docs/azkoyen-hopper-protocol.md` section 2.1
- **Setup guide:** [hardware/README.md](../hardware/README.md#-critical-configuration-requirements)

### 2. Optocoupler Resistor Modification

PC817 module #1 (motor control, channel D1) requires resistor modification:
- Add 330Ω in parallel with R1 (stock 1kΩ) → 248Ω total
- Provides 13.3mA drive current for proper saturation
- Without this: motor control unreliable or non-functional

**Firmware assumes:**
- Hopper in NEGATIVE mode (active LOW control)
- Optocoupler wiring: D1 → IN+, GND → IN-
- GPIO HIGH → LED ON → OUT LOW → motor ON
- GPIO LOW → LED OFF → OUT HIGH → motor OFF

---
```

**Step 2: Update Troubleshooting section**

Add to the "Common Issues" section:

```markdown
### Motor Control Issues

#### Motor doesn't engage during dispense

**Check hardware first:**
1. Hopper DIP switch set to NEGATIVE mode (not POSITIVE)
2. Optocoupler R1 modified (330Ω in parallel with 1kΩ)
3. VCC pin on optocoupler connected to 12V

**Verify with multimeter:**
- D1 pin HIGH during dispense: should measure 3.3V
- Optocoupler OUT LOW during dispense: should be < 0.5V to GND
- Hopper control pin (7) LOW during dispense: should be < 0.5V to GND

**If control pin HIGH when should be LOW:**
- Hopper is in POSITIVE mode → change DIP switch to NEGATIVE

**If OUT voltage too high (> 1V) when LED ON:**
- R1 not modified correctly → verify 330Ω parallel resistor
- Measure IN+ to IN-: should be ~250Ω, not 1kΩ

#### Motor engages at wrong time (after timeout instead of during dispense)

**Cause:** Hopper in POSITIVE mode (active HIGH) instead of NEGATIVE mode (active LOW).

**Fix:** Open hopper, set DIP switch to NEGATIVE, retest.

---
```

**Step 3: Update Pin Configuration table**

Find the GPIO pin table and update the MOTOR_PIN row:

```markdown
| Pin | GPIO | Function | Details |
|-----|------|----------|---------|
| D1  | GPIO5 | Motor Control Output | Via PC817 optocoupler #1 (active LOW with NEGATIVE mode). **Wiring: D1→IN+, GND→IN-, VCC→12V**. GPIO HIGH = LED ON = OUT LOW = motor ON. Requires R1 modification (330Ω parallel). |
```

**Step 4: Commit firmware README updates**

```bash
git add firmware/README.md
git commit -m "docs(firmware): add critical hardware configuration requirements

- Add warning about NEGATIVE mode requirement before flashing
- Document optocoupler resistor modification prerequisite
- Add troubleshooting for motor control issues
- Clarify GPIO→optocoupler→hopper signal chain"
```

---

## Task 3: Update Main README

**Files:**
- Modify: `README.md`

**Step 1: Add "Quick Start Prerequisites" section**

After the "Features" section, before "Hardware Components", add:

```markdown
## ⚠️ Before You Start - Critical Setup Requirements

**These configurations are mandatory. Incorrect settings will cause motor control failures:**

### 1. Hopper Configuration: NEGATIVE Mode

The Azkoyen Hopper U-II has a DIP switch for control signal polarity. **It MUST be set to NEGATIVE mode.**

- ✅ Correct: **NEGATIVE** mode (active LOW - control pin LOW = motor ON)
- ❌ Wrong: POSITIVE mode (causes inverted motor behavior)

**Symptom of wrong mode:** Motor doesn't engage during dispense, or engages at wrong times.

**How to fix:** Open hopper, locate DIP switches, set "NEGATIVE/POSITIVE" switch to NEGATIVE position.

See [hardware/README.md](hardware/README.md#-critical-configuration-requirements) for detailed instructions.

---

### 2. Optocoupler Resistor Modification Required

The PC817 optocoupler modules (bestep brand) require hardware modification for reliable operation.

**What to modify:**
- Motor control optocoupler (channel #1): Add 330Ω resistor in parallel with R1 (stock 1kΩ)
- This provides proper 13.3mA drive current for saturation

**Without this modification:**
- Output voltage won't drop low enough (stays at 3-7V instead of < 0.5V)
- Motor control unreliable or non-functional

See [hardware/README.md - Electronic Components](hardware/README.md#electronic-components) for modification procedure.

---
```

**Step 2: Update GPIO pin description in main table**

Find the pin description table and update D1 entry:

```markdown
- **D1** (GPIO5) ← Motor control output (via PC817 optocoupler #1) - **Active LOW** (with NEGATIVE mode)
  - ⚠️ Requires optocoupler R1 modification (330Ω parallel resistor)
  - Wiring: D1→IN+, so GPIO HIGH = motor ON
```

**Step 3: Commit main README updates**

```bash
git add README.md
git commit -m "docs: add critical prerequisites section for hopper NEGATIVE mode and resistor mod

- Add prominent warning about NEGATIVE mode requirement
- Document optocoupler modification as prerequisite
- Link to detailed hardware setup guide
- Prevent hours of debugging for new users"
```

---

## Task 4: Update CLAUDE.md Project Instructions

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Update "Hardware Interfaces" section**

Replace the ESP8266 ↔ Azkoyen Hopper section with:

```markdown
### ESP8266 ↔ Azkoyen Hopper

**⚠️ CRITICAL: Hopper MUST be in NEGATIVE mode (active LOW control)**

- **Power:** 12V/2A DC adapter with 2200µF capacitor for motor startup surge
- **Isolation:** 4× PC817 optocoupler modules (bestep brand) for galvanic isolation
- **Control logic:** Hopper set to NEGATIVE mode, so control pin active LOW (LOW = motor ON)
- **Optocoupler wiring:** D1 → IN+, GND → IN- (not inverted), so:
  - GPIO HIGH → optocoupler LED ON → OUT LOW (< 0.5V) → motor ON
  - GPIO LOW → optocoupler LED OFF → OUT HIGH (~6V with hopper load) → motor OFF

**Pin assignments:**
- D1 (GPIO5) → Control output (via PC817 #1) - **Requires R1 modification (330Ω parallel)**
- D7 (GPIO13) ← Coin pulse input (via PC817 #2) - active LOW, FALLING edge interrupt
- D5 (GPIO14) ← Error signal input (via PC817 #3) - active LOW
- D6 (GPIO12) ← Empty sensor input (via PC817 #4) - active LOW

**Optocoupler Resistor Requirements:**

PC817 modules (bestep brand) ship with inadequate resistor values:
- **R1 (input) = 1kΩ stock** → Too high! Only 3.3mA drive current
  - **Modification required:** Add 330Ω in parallel → 248Ω total → 13.3mA drive ✓
  - **Applies to:** Motor control optocoupler (channel #1, pin D1)
  - **Without this:** Phototransistor won't saturate, unreliable motor control
- **R2 (output) = 10kΩ stock** → Acceptable (weak pull-up, but works with NEGATIVE mode)
  - Creates voltage divider with hopper input impedance (~10kΩ)
  - Result: HIGH = ~6V (acceptable), LOW = ~0.1V (good)

**Why wiring is D1→IN+ (not 3.3V→IN+):**
- With D1→IN+ and GND→IN-: GPIO HIGH provides current through LED
- Alternative wiring (3.3V→IN+, D1→IN-) would require inverted firmware logic
- Current wiring matches common optocoupler usage patterns
```

**Step 2: Add "Development Notes - Critical Lessons" section**

After the "Development Notes" section, add:

```markdown
### Critical Lessons Learned

**Hopper NEGATIVE Mode Requirement:**
- The Azkoyen Hopper U-II has a NEGATIVE/POSITIVE DIP switch for control polarity
- **NEGATIVE mode is mandatory** for active LOW control (LOW = motor ON)
- POSITIVE mode causes inverted behavior: motor runs at wrong times
- **Debugging symptom:** Motor engages AFTER timeout instead of during dispense
- **Fix:** Open hopper, verify DIP switch set to NEGATIVE

**Optocoupler Drive Current:**
- PC817 requires 10-20mA input current for saturation
- Stock bestep modules have R1 = 1kΩ → only 3.3mA from 3.3V GPIO
- **Insufficient current causes:** OUT stays at 3-7V instead of dropping to 0V
- **Solution:** Add 330Ω in parallel with R1 → 248Ω → 13.3mA ✓
- **Measurement test:** LED ON should pull OUT to < 0.5V, not 3-7V

**Voltage Divider with Hopper Input:**
- Hopper control input has ~10kΩ impedance to ground
- With R2 = 10kΩ pull-up, creates voltage divider
- HIGH level = 12V × (10kΩ / 20kΩ) = 6V (not 12V)
- This is acceptable: NEGATIVE mode threshold ~3-4V, so 6V = HIGH ✓

**Signal Chain for Motor Control:**
```
GPIO HIGH (3.3V)
  → Current through 248Ω (modified R1) and optocoupler LED (13.3mA)
  → Phototransistor saturates
  → OUT pulled to GND (< 0.5V)
  → Hopper control pin LOW
  → NEGATIVE mode: motor ON ✓
```

**Time spent debugging these issues:** ~4 hours

**Root causes:**
1. Hopper was in POSITIVE mode, not documented as critical requirement
2. Optocoupler modules have inadequate stock resistors, not obvious without measurement
3. Voltage divider behavior not documented, caused confusion about "correct" voltages
```

**Step 3: Commit CLAUDE.md updates**

```bash
git add CLAUDE.md
git commit -m "docs(claude): add critical lessons learned about hopper config and optocoupler

- Document NEGATIVE mode requirement for active LOW control
- Explain optocoupler resistor modification necessity (R1 = 1kΩ too high)
- Clarify voltage divider behavior with hopper input impedance
- Add signal chain diagram for motor control
- Record 4 hours debugging time to emphasize importance"
```

---

## Task 5: Update Code Comments in config.h

**Files:**
- Modify: `firmware/dispenser/config.h`

**Step 1: Update GPIO pin comment block**

Replace the current GPIO Pins comment block with:

```cpp
// GPIO Pins (Wemos D1 Mini ESP8266)
//
// ⚠️ CRITICAL HARDWARE REQUIREMENTS:
//   1. Hopper DIP switch MUST be set to NEGATIVE mode (active LOW control)
//   2. PC817 optocoupler #1 (motor control) R1 MUST be modified: add 330Ω in parallel with stock 1kΩ
//
// OPTOCOUPLER WIRING (PC817 modules with D1→IN+, GND→IN-):
//   - Motor control (D1): GPIO HIGH → LED ON → OUT LOW (~0.1V) → motor ON (NEGATIVE mode)
//                         GPIO LOW  → LED OFF → OUT HIGH (~6V) → motor OFF
//   - Input signals: LOW = signal active (coin pulse/error/empty detected)
//
// WHY THESE VALUES:
//   - R1 modification (1kΩ || 330Ω = 248Ω): Provides 13.3mA for PC817 saturation
//     Without modification: Only 3.3mA → phototransistor won't saturate → unreliable control
//   - R2 (10kΩ pull-up): Creates voltage divider with hopper input (~10kΩ) → HIGH = ~6V
//     This is acceptable for NEGATIVE mode (threshold ~3-4V)
//   - OUT voltage ranges: LED ON < 0.5V (reliable LOW), LED OFF ~6V (reliable HIGH for NEGATIVE mode)
```

**Step 2: Add NEGATIVE mode note to timing constants**

After the `#define MAX_TOKENS` line, add:

```cpp
// Hopper Mode (configured via DIP switches inside hopper)
// ⚠️ REQUIRED: Set to NEGATIVE mode for active LOW control
// POSITIVE mode will cause inverted motor behavior (motor runs at wrong times)
#define HOPPER_MODE_NEGATIVE  // Document the required mode (not a code constant)
```

**Step 3: Commit config.h updates**

```bash
git add firmware/dispenser/config.h
git commit -m "docs(firmware): clarify optocoupler wiring and NEGATIVE mode requirement in config.h

- Add critical hardware requirement warnings
- Explain R1 modification necessity (13.3mA for saturation)
- Document voltage ranges and why 6V HIGH is acceptable
- Add NEGATIVE mode documentation comment"
```

---

## Task 6: Update Code Comments in hopper_control.cpp

**Files:**
- Modify: `firmware/dispenser/hopper_control.cpp`

**Step 1: Add file header comment**

At the top of the file, after `#include "hopper_control.h"`, add:

```cpp
// firmware/dispenser/hopper_control.cpp

// Motor Control Signal Chain (with NEGATIVE mode hopper and modified optocoupler):
//
//   startMotor() → digitalWrite(MOTOR_PIN, HIGH)
//     → D1 = 3.3V
//     → Current through optocoupler LED: 3.3V / 248Ω (modified R1) = 13.3mA
//     → PC817 phototransistor saturates
//     → OUT pulled LOW (< 0.5V)
//     → Hopper control pin LOW
//     → NEGATIVE mode: motor ON ✓
//
//   stopMotor() → digitalWrite(MOTOR_PIN, LOW)
//     → D1 = 0V
//     → No current through optocoupler LED
//     → PC817 phototransistor OFF
//     → OUT pulled HIGH by R2 (10kΩ) to ~6V (voltage divider with hopper input)
//     → Hopper control pin HIGH (~6V)
//     → NEGATIVE mode: motor OFF ✓
//
// ⚠️ CRITICAL HARDWARE DEPENDENCIES:
//   - Hopper DIP switch in NEGATIVE mode (active LOW)
//   - Optocoupler R1 modified (330Ω parallel) for 13.3mA drive current
//   - Without these: motor behavior unreliable or inverted
```

**Step 2: Update begin() function comments**

Update the motor pin initialization comment:

```cpp
  // Configure GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);  // Motor off at startup
  // Note: With D1→IN+ wiring, LOW = LED off = OUT high (~6V) = motor OFF (NEGATIVE mode)
  Serial.print("[HopperControl] MOTOR_PIN (D1) configured as OUTPUT, set to LOW (motor OFF)");
```

**Step 3: Update startMotor() and stopMotor() comments**

```cpp
void HopperControl::startMotor() {
  Serial.println("[HopperControl] *** STARTING MOTOR ***");
  Serial.print("  Setting MOTOR_PIN (D1) to HIGH (motor ON)...");
  // GPIO HIGH → optocoupler LED ON → OUT LOW → motor ON (NEGATIVE mode)
  // Requires: R1 modified (330Ω parallel) for 13.3mA → saturation → OUT < 0.5V
  digitalWrite(MOTOR_PIN, HIGH);
  Serial.print(" - Current state: ");
  Serial.println(digitalRead(MOTOR_PIN));
  last_pulse_time = millis();  // Reset watchdog
  Serial.println("[HopperControl] Motor started, watchdog reset");
}

void HopperControl::stopMotor() {
  Serial.println("[HopperControl] *** STOPPING MOTOR ***");
  Serial.print("  Setting MOTOR_PIN (D1) to LOW (motor OFF)...");
  // GPIO LOW → optocoupler LED OFF → OUT HIGH (~6V) → motor OFF (NEGATIVE mode)
  digitalWrite(MOTOR_PIN, LOW);
  Serial.print(" - Current state: ");
  Serial.println(digitalRead(MOTOR_PIN));
  Serial.println("[HopperControl] Motor stopped");
}
```

**Step 4: Commit hopper_control.cpp updates**

```bash
git add firmware/dispenser/hopper_control.cpp
git commit -m "docs(firmware): add detailed signal chain comments for motor control

- Add file header explaining GPIO → optocoupler → hopper signal chain
- Document voltage levels at each stage
- Clarify NEGATIVE mode dependency
- Add inline comments for startMotor/stopMotor explaining control flow"
```

---

## Task 7: Create Dedicated Troubleshooting Guide

**Files:**
- Create: `docs/troubleshooting/motor-control-issues.md`

**Step 1: Create troubleshooting directory and file**

```bash
mkdir -p docs/troubleshooting
```

**Step 2: Write comprehensive troubleshooting guide**

```markdown
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
- \> 1V → R1 needs modification

---

**Last Updated:** 2026-02-13
**Based on:** Extensive debugging session resolving motor control issues
```

**Step 3: Commit troubleshooting guide**

```bash
git add docs/troubleshooting/motor-control-issues.md
git commit -m "docs: add comprehensive motor control troubleshooting guide

- Document 4 most common motor control issues with solutions
- Add diagnostic flowchart and voltage reference table
- Include hardware modification checklist
- Reduces debugging time from hours to minutes

Based on real debugging session 2026-02-13."
```

---

## Task 8: Update Wiring Diagram Annotations (Optional - Manual)

**Files:**
- Reference: `docs/wiring-diagram-optocoupler.svg`
- Reference: `docs/pinout-diagram-optocoupler.svg`

**Note:** SVG editing requires manual intervention. This task documents what should be added.

**Step 1: Document required SVG changes**

Create a note file documenting needed diagram updates:

```bash
cat > docs/wiring-diagram-updates-needed.md << 'EOF'
# Wiring Diagram Updates Needed

## Files to Update
- `docs/wiring-diagram-optocoupler.svg`
- `docs/pinout-diagram-optocoupler.svg`

## Required Annotations

### For wiring-diagram-optocoupler.svg:

**Add text annotations:**

1. Near optocoupler #1 (motor control):
   ```
   ⚠️ MODIFICATION REQUIRED
   Add 330Ω resistor in parallel with R1 (stock 1kΩ)
   Result: 248Ω → 13.3mA drive current
   ```

2. Near hopper DIP switch section (if shown):
   ```
   ⚠️ CRITICAL: Set to NEGATIVE mode
   (Active LOW: control pin LOW = motor ON)
   ```

3. Near D1 connection:
   ```
   Wiring: D1 → IN+, GND → IN-
   GPIO HIGH → LED ON → OUT LOW → motor ON
   ```

**Add color coding:**
- Highlight optocoupler #1 in orange/yellow (needs modification)
- Highlight optocouplers #2, #3, #4 in green (stock OK)

### For pinout-diagram-optocoupler.svg:

**Add note near D1 pin:**
```
D1 (GPIO5) - Motor Control
⚠️ Optocoupler R1 must be modified
Add 330Ω || 1kΩ = 248Ω
```

## How to Edit SVGs:

1. Open in Inkscape or other SVG editor
2. Add text elements with annotations above
3. Use warning colors (orange/red) for critical notes
4. Maintain existing layout and readability
5. Export as SVG, ensure compatibility

## Alternative: Add Note in README

If SVG editing is too complex, add a prominent note in hardware/README.md
before the diagram images pointing to the modification requirements.

EOF
```

**Step 2: Commit diagram update documentation**

```bash
git add docs/wiring-diagram-updates-needed.md
git commit -m "docs: document needed wiring diagram annotations

SVG updates required to highlight R1 modification and NEGATIVE mode.
Manual editing needed in Inkscape or similar tool."
```

---

## Task 9: Create Summary Document for Future Reference

**Files:**
- Create: `docs/critical-findings-2026-02-13.md`

**Step 1: Create summary document**

```markdown
# Critical Findings: Motor Control Debug Session (2026-02-13)

**Time Invested:** ~4 hours
**Issues Found:** 2 critical configuration errors
**Impact:** Complete motor control failure until resolved

---

## Summary

During initial hardware bring-up, motor control completely failed with confusing symptoms:
- Motor didn't engage during dispense requests
- Motor engaged at wrong times (after timeout)
- Optocoupler appeared to work (LED bright, voltages measured) but motor didn't respond

**Root causes:**
1. Hopper DIP switch in POSITIVE mode (should be NEGATIVE)
2. Optocoupler input resistor too high (stock 1kΩ, needs 330Ω parallel)

---

## Issue 1: Hopper POSITIVE vs NEGATIVE Mode

### The Problem

Azkoyen Hopper U-II has DIP switch for control signal polarity:
- **NEGATIVE mode:** Control pin active LOW (LOW = motor ON, HIGH = motor OFF)
- **POSITIVE mode:** Control pin active HIGH (HIGH = motor ON, LOW = motor OFF)

**Unit was in POSITIVE mode, design assumes NEGATIVE mode.**

### Symptoms

- Motor didn't engage when LED turned ON (control pin went LOW)
- Motor DID engage when LED turned OFF (control pin went HIGH ~6V)
- Completely inverted behavior from expected

### Discovery Process

1. Initially assumed hopper was always active LOW (from old documentation)
2. Measured voltages - confirmed OUT going LOW when LED ON
3. Manually tested: connecting pin 7 to GND made motor run → confirmed active LOW expected
4. But motor ran when OUT was HIGH, not LOW → contradictory!
5. User suggested checking "PULSE mode" → realized there might be other DIP switches
6. **User insight:** "We are misinterpreting what PULSE means! It used to be set to negative before!"
7. Checked DIP switch: was in POSITIVE mode
8. Switched to NEGATIVE mode → **motor worked immediately**

### Lesson

**NEGATIVE mode is mandatory, not optional. Must be documented prominently.**

---

## Issue 2: PC817 Optocoupler Input Resistor Too High

### The Problem

PC817 optocoupler modules (bestep brand) ship with:
- **R1 (input) = 1kΩ** → provides only 3.3mA from 3.3V GPIO
- PC817 needs **10-20mA** for proper saturation

With insufficient current:
- Phototransistor weakly conducting
- OUT voltage doesn't drop to near 0V (stays at 3-7V)
- Motor control unreliable

### Symptoms

- LED bright red (seemed like good current, but wasn't enough)
- OUT measured 3-7V when LED ON (should be < 0.5V)
- Sometimes motor worked, sometimes didn't
- Unreliable, inconsistent operation

### Discovery Process

1. Initially thought optocoupler was working (LED bright)
2. Measured OUT: 3V when LED ON → not low enough
3. Connected VCC to 12V for pull-up: improved to 6V HIGH, but still 3V LOW (not good enough)
4. Calculated input current: 3.3V / 1kΩ = 3.3mA → **too low!**
5. Added 330Ω in parallel with R1: (1kΩ || 330Ω) = 248Ω → 13.3mA
6. OUT now measured < 0.2V when LED ON → **motor control reliable**

### Additional Complication: R2 Pull-Up

Initially thought R2 (10kΩ output pull-up) was too weak because:
- LED OFF measured only 6V on OUT (expected ~12V)

**Why 6V is actually correct:**
- Hopper control input has ~10kΩ impedance to ground
- Forms voltage divider: 12V × (10kΩ / (10kΩ + 10kΩ)) = 6V
- With NEGATIVE mode, 6V is HIGH enough (threshold ~3-4V)

**Attempted fix that didn't help:**
- Added 1kΩ in parallel with R2 → stronger pull-up (909Ω total)
- This made HIGH voltage better (10-11V)
- BUT made saturation harder (more current to sink)
- With weak input current (3.3mA), couldn't pull LOW enough
- **Solution:** Fix input current first, then weak R2 is acceptable

**Final configuration:**
- R1: 248Ω (1kΩ || 330Ω) → 13.3mA input current
- R2: 10kΩ (stock) → adequate pull-up
- Result: LOW < 0.5V, HIGH ~6V → reliable operation

### Lesson

**PC817 modules with 1kΩ input resistor cannot saturate from 3.3V GPIO.**
**Modification mandatory: add 330Ω parallel resistor for 10-15mA drive.**

---

## Debugging Timeline

**Hour 0-1: Initial confusion**
- Motor didn't engage, assumed code logic error
- Tried inverting GPIO logic multiple times
- Each inversion made problem worse or same

**Hour 1-2: Voltage measurements**
- Measured optocoupler OUT: confusing values (2.8V, 6V, changing)
- Realized VCC not connected → fixed, but still wrong voltages
- LED bright but OUT not low enough

**Hour 2-3: Resistor modifications**
- Identified R1 too high → added 330Ω parallel
- Still not working → added R2 parallel (1kΩ)
- Better voltages but motor behavior still backwards

**Hour 3-4: The breakthrough**
- Motor engaged when LED went OFF (inverted!)
- User insight about "PULSE mode" and "NEGATIVE" setting
- Checked hopper DIP switch → was in POSITIVE mode
- **Switched to NEGATIVE → immediate success**

**Post-fix verification:**
- Removed R2 parallel resistor (not needed)
- Final config: R1 modified, R2 stock, NEGATIVE mode
- Reliable operation: LOW < 0.5V, HIGH ~6V, motor engages correctly

---

## What Went Wrong (Process)

1. **Assumption:** Hopper is always active LOW (from documentation)
   - **Reality:** Has POSITIVE/NEGATIVE mode switch, was in wrong mode

2. **Missing info:** Optocoupler module resistor values not checked initially
   - **Reality:** Stock R1 inadequate for 3.3V drive

3. **Confusing signals:** LED brightness suggested good current
   - **Reality:** LED bright at 3.3mA, but PC817 needs 10-20mA for saturation

4. **Voltage divider:** 6V seemed wrong (expected 12V)
   - **Reality:** Hopper input impedance creates divider, 6V is normal and acceptable

---

## Documentation Gaps (Fixed by This Plan)

**Before:**
- No mention of NEGATIVE mode requirement
- No warning about optocoupler resistor values
- No explanation of voltage divider behavior
- No troubleshooting guide for motor control
- No extracted protocol documentation from PDF

**After (implemented by this plan):**
- ✅ **Protocol document extracted:** `docs/azkoyen-hopper-protocol.md` from official PDF
- ✅ **Referenced from CLAUDE.md** as mandatory reading for all hopper work
- Prominent warnings in all README files
- Detailed resistor modification instructions
- Voltage level reference tables with official thresholds (< 0.5V, > 4V)
- Comprehensive troubleshooting guide
- Code comments explaining signal chain
- All documentation references official protocol specifications

---

## Key Measurements for Future Reference

**Correct operation (NEGATIVE mode, R1 modified):**

| Point | LED OFF | LED ON |
|-------|---------|--------|
| D1 GPIO | 0V | 3.3V |
| Optocoupler IN+ to IN- resistance | N/A | ~250Ω (powered off) |
| Optocoupler OUT (no load) | ~12V | < 0.2V |
| Optocoupler OUT (with hopper) | ~6V | < 0.5V |
| Hopper control pin 7 | ~6V | < 0.5V |
| Motor state | OFF | ON |

**If your measurements don't match this table, see troubleshooting guide.**

---

## Checklist for Future Builds

**Before powering on:**
- [ ] Hopper DIP switch verified: NEGATIVE mode
- [ ] Optocoupler #1 R1 modified: 330Ω in parallel (measure ~250Ω IN+ to IN-)
- [ ] Optocoupler VCC connected to 12V
- [ ] All wiring double-checked: D1→IN+, GND→IN-, OUT→hopper pin 7

**After power on:**
- [ ] Measure D1: should be LOW (0V) at startup
- [ ] Measure OUT: should be ~6V at startup (motor off)
- [ ] Trigger dispense: D1 goes HIGH (3.3V)
- [ ] Measure OUT during dispense: should be < 0.5V (motor on)
- [ ] Hopper motor engages immediately when dispense triggered
- [ ] Motor stops when LED turns off

**If any checkbox fails, use troubleshooting guide before proceeding.**

---

## Files Updated by This Documentation Plan

**Documentation:**
- `hardware/README.md` - Critical warnings section, resistor modification instructions
- `firmware/README.md` - Hardware prerequisites, troubleshooting
- `README.md` - Quick start prerequisites section
- `CLAUDE.md` - Lessons learned, voltage divider explanation
- `docs/troubleshooting/motor-control-issues.md` - Comprehensive troubleshooting guide
- `docs/critical-findings-2026-02-13.md` - This document

**Code:**
- `firmware/dispenser/config.h` - NEGATIVE mode requirement, resistor value comments
- `firmware/dispenser/hopper_control.cpp` - Signal chain explanation, inline comments

**Diagrams (documented, manual update needed):**
- `docs/wiring-diagram-updates-needed.md` - Required SVG annotation changes

---

**Estimated Time Saved for Future Developers:** 3-4 hours per build
**Likelihood of Hitting These Issues:** ~100% (hardware comes with wrong defaults)

**ROI of This Documentation:** High - prevents guaranteed multi-hour debugging session

---
```

**Step 2: Commit summary document**

```bash
git add docs/critical-findings-2026-02-13.md
git commit -m "docs: add critical findings summary from motor control debug session

- Document 4-hour debugging timeline and root causes
- Explain NEGATIVE mode requirement discovery process
- Detail optocoupler resistor modification necessity
- Provide reference measurements and checklist
- Record lessons learned to prevent future issues"
```

---

## Task 10: Final Review and Integration

**Step 1: Verify all files updated**

```bash
# List all files modified by this plan
git status

# Expected changes:
# - hardware/README.md
# - firmware/README.md
# - README.md
# - CLAUDE.md
# - firmware/dispenser/config.h
# - firmware/dispenser/hopper_control.cpp
# - docs/troubleshooting/motor-control-issues.md (new)
# - docs/critical-findings-2026-02-13.md (new)
# - docs/wiring-diagram-updates-needed.md (new)
# - docs/plans/2026-02-13-hopper-configuration-critical-documentation.md (new, this plan)
```

**Step 2: Create summary commit (optional)**

If all changes committed individually, create summary tag:

```bash
git tag -a v1.0-motor-control-docs -m "Critical motor control documentation complete

Added comprehensive documentation for:
- Hopper NEGATIVE mode requirement
- PC817 optocoupler resistor modifications
- Voltage level references and troubleshooting

Prevents 3-4 hours of debugging for future developers."
```

**Step 3: Update main README with link to troubleshooting**

Add to README.md "Quick Start Prerequisites" section:

```markdown
**Having motor control issues?** See [Motor Control Troubleshooting Guide](docs/troubleshooting/motor-control-issues.md) - solves 90% of problems in minutes.
```

Commit:
```bash
git add README.md
git commit -m "docs: add link to motor control troubleshooting guide in README"
```

**Step 4: Verification complete**

All documentation updated with critical findings. Future developers will have:
- ✅ Prominent warnings about NEGATIVE mode requirement
- ✅ Clear instructions for optocoupler resistor modification
- ✅ Comprehensive troubleshooting guide
- ✅ Reference voltage tables and measurements
- ✅ Detailed explanation of debugging process and lessons learned

---

## Completion

**Total Tasks:** 10
**Files Created:** 4 (includes `docs/azkoyen-hopper-protocol.md` - **already completed**)
**Files Modified:** 8 (includes `CLAUDE.md` updates - **already completed**)
**Estimated Time to Implement:** 60-90 minutes (remaining tasks)
**Time Saved for Future Developers:** 3-4 hours per build

**Already Completed (before plan execution):**
- ✅ `docs/azkoyen-hopper-protocol.md` - Official protocol specification extracted from PDF
- ✅ `CLAUDE.md` - Updated to reference protocol doc as mandatory reading

**Critical Success Factor:** Every new developer/user will see the warnings BEFORE attempting hardware setup, preventing the 4-hour debugging session entirely. Protocol document provides authoritative reference for all voltage levels, modes, and timing specifications.
