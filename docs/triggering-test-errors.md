# Triggering Test Errors on Azkoyen Hopper

Guide for safely triggering each error type for testing the error decoding system.

## Safety First ⚠️

- **Unplug 12V power** before making physical changes
- **Never force mechanical parts** - could damage hopper
- **Test one error at a time** - easier to debug
- **Take photos** before modifying anything
- **Power cycle** to reset errors between tests

## Error Test Methods (Easiest to Hardest)

### ✅ Error 6: Sensor Fault (EASIEST)

**How to trigger:** Disconnect the coin exit sensor

**Steps:**
1. Power off hopper (unplug 12V)
2. Locate the coin exit sensor connector (usually at opto board)
3. Unplug the photocell connector
4. Power on hopper
5. Start a dispense operation

**Expected behavior:**
- Hopper detects sensor is disconnected immediately
- Error signal: 100ms + 6×10ms pulses
- Motor may not start or stops immediately

**Serial output:**
```
[ErrorDecoder] Error detected: SENSOR_FAULT - Exit sensor disconnected/faulty
```

**HTTP output:**
```json
{
  "error": {
    "active": true,
    "code": 6,
    "type": "SENSOR_FAULT",
    "description": "Exit sensor disconnected/faulty"
  }
}
```

**To reset:**
1. Power off
2. Reconnect sensor
3. Power on
4. Start successful dispense (clears error)

---

### ✅ Error 7: Power Fault (EASY with equipment)

**How to trigger:** Use power supply outside rated range

**Requirements:**
- Variable DC power supply (not wall adapter)
- Voltage range capability: 0-20V

**Steps:**
1. Replace 12V adapter with variable power supply
2. Set to very low voltage (e.g., 6V) or very high (e.g., 18V)
3. Power on hopper
4. Start dispense operation

**Expected behavior:**
- Hopper detects voltage out of spec
- Error signal: 100ms + 7×10ms pulses

**⚠️ Warning:** Don't exceed maximum voltage rating! Check hopper specs (usually 12V ±20%).

**Serial output:**
```
[ErrorDecoder] Error detected: POWER_FAULT - Power supply out of range
```

**To reset:**
1. Set power supply back to 12V
2. Start successful dispense

---

### ⚠️ Error 1: Coin Stuck (MEDIUM - requires coins/tokens)

**How to trigger:** Block coin exit sensor with object

**Steps:**
1. Power off hopper
2. Load tokens/coins into hopper
3. Insert a thin piece of tape or cardboard into the coin exit path
   - Should block/cover the photocell beam
   - Don't jam mechanically - just cover optical sensor
4. Power on
5. Start dispense

**Expected behavior:**
- Motor starts, coin begins to exit
- Coin blocks sensor for >65ms (max pulse time)
- Error signal: 100ms + 1×10ms pulse
- Motor stops

**Alternative method (cleaner):**
- Use a small magnet or piece of metal near the sensor
- Triggers optical sensor without mechanical blockage

**Serial output:**
```
[ErrorDecoder] Error detected: COIN_STUCK - Coin stuck in exit sensor (>65ms)
```

**To reset:**
1. Power off
2. Remove blocking object
3. Manually clear any partially-dispensed coin
4. Power on
5. Start successful dispense

---

### ⚠️ Error 2: Sensor OFF (MEDIUM)

**How to trigger:** Block photocell so it stays dark

**Steps:**
1. Power off hopper
2. Cover the coin exit photocell with opaque tape
   - Black electrical tape works well
   - Cover both photodiode and phototransistor
3. Power on
4. Start dispense

**Expected behavior:**
- Hopper sees sensor permanently blocked
- Error signal: 100ms + 2×10ms pulses

**Serial output:**
```
[ErrorDecoder] Error detected: SENSOR_OFF - Exit sensor stuck OFF
```

**To reset:**
1. Power off
2. Remove tape from sensor
3. Power on
4. Start successful dispense

---

### ⚠️ Error 3: Permanent Jam (HARDER - requires coins)

**How to trigger:** Create mechanical jam that hopper can't clear

**Steps:**
1. Power off hopper
2. Load only 1-2 coins (so it runs out mid-dispense)
3. Request dispense of many coins (e.g., 10 tokens)
4. Let hopper run out of coins
5. Hopper will attempt to clear "jam" multiple times
6. After max retries, reports permanent jam

**Alternative method:**
- Partially block coin path with finger/object during dispense
- Hopper retries several times, then gives up

**Expected behavior:**
- Motor runs but no coins dispense
- Multiple retry attempts
- Error signal: 100ms + 3×10ms pulses

**Serial output:**
```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
```

**To reset:**
1. Power off
2. Reload coins
3. Clear any mechanical obstruction
4. Power on
5. Start successful dispense

---

### ⚠️ Error 5: Motor Fault (RISKY - not recommended)

**How to trigger:** Disconnect motor power

**⚠️ WARNING:** Only do this if you're comfortable with electrical work!

**Steps:**
1. Power off completely (unplug 12V AND ESP8266)
2. Disconnect motor connector from hopper control board
3. Power on
4. Start dispense command

**Expected behavior:**
- Hopper tries to start motor
- No motor voltage detected
- Error signal: 100ms + 5×10ms pulses

**Risks:**
- Might damage control board if done incorrectly
- Hard to reconnect if connector is internal

**Serial output:**
```
[ErrorDecoder] Error detected: MOTOR_FAULT - Motor doesn't start
```

**Better alternative:** Skip this error - rare in practice, risky to test.

---

### 🔧 Error 4: Max Span (HARDEST - not practical)

**How to trigger:** Multiple jam attempts exceeding max time

**Why skip:**
- Requires triggering 3+ consecutive jam/retry cycles
- Complex timing requirements
- High risk of confusing hopper state
- Not worth the effort for testing

**If you must test:**
1. Trigger Error 3 (permanent jam)
2. Before it completes, manually cause another jam
3. Repeat to exceed max spans

**Better alternative:** Trust that if Error 3 works, Error 4 logic works too.

---

## Recommended Test Sequence

For systematic testing, do these in order:

### Test 1: Error 6 (Sensor Fault)
**Time:** 2 minutes
**Difficulty:** ⭐ Easy
**Why first:** Safest, easiest to trigger and reset

1. Disconnect coin sensor
2. Start dispense
3. Verify error code 6 detected
4. Check HTTP endpoint shows error
5. Check TUI shows "SENSOR_FAULT" in red
6. Reconnect sensor
7. Successful dispense clears error
8. Verify error moved to history with `cleared: true`

### Test 2: Error 2 (Sensor OFF)
**Time:** 3 minutes
**Difficulty:** ⭐ Easy
**Why second:** Tests different sensor failure mode

1. Cover sensor with tape
2. Start dispense
3. Verify error code 2
4. Check error appears in TUI
5. Remove tape
6. Successful dispense clears error

### Test 3: Error 1 (Coin Stuck)
**Time:** 5 minutes
**Difficulty:** ⭐⭐ Medium
**Needs:** Coins/tokens

1. Block coin exit path lightly
2. Start dispense
3. Verify error code 1
4. Remove blockage
5. Clear any stuck coins
6. Successful dispense clears error

### Test 4: Error 3 (Permanent Jam)
**Time:** 5 minutes
**Difficulty:** ⭐⭐ Medium
**Needs:** Coins/tokens

1. Load only 1 coin
2. Request 10 coins
3. Wait for retry attempts
4. Verify error code 3
5. Reload coins
6. Successful dispense clears error

### Test 5: Ring Buffer Wrap
**Time:** 10 minutes
**Difficulty:** ⭐⭐ Medium

1. Trigger errors 6, 2, 1, 3, 6, 2, 1 (total 7)
2. Check HTTP: `error_history` shows only last 5
3. Verify oldest errors dropped (FIFO)
4. Clear active error
5. Check TUI shows ✓ for cleared errors

---

## Testing Without Hopper Hardware

If you don't have the hopper connected yet:

### Method 1: Manual Pulse Generation

Use a jumper wire and manual timing:

1. Connect jumper from D5 to a pushbutton to GND
2. Use a timer on your phone
3. Press pattern:
   - Hold 100ms (start pulse)
   - Release 20ms
   - Hold 10ms (code pulse 1)
   - Release 10ms
   - Hold 10ms (code pulse 2)
   - ...repeat for N pulses

**Pros:** No extra hardware needed
**Cons:** Timing is hard to get right manually

### Method 2: Arduino Simulator

Use a second Arduino/ESP8266 to generate precise test pulses:

```cpp
// Upload this to a second ESP8266
#define TEST_OUT D1  // Connect to main ESP8266 D5

void setup() {
  pinMode(TEST_OUT, OUTPUT);
  digitalWrite(TEST_OUT, HIGH);  // Rest state
  Serial.begin(9600);
  delay(2000);
}

void loop() {
  Serial.println("Press 1-7 to simulate error code");

  if (Serial.available()) {
    int code = Serial.read() - '0';
    if (code >= 1 && code <= 7) {
      simulateError(code);
    }
  }
}

void simulateError(int errorCode) {
  Serial.print("Simulating error ");
  Serial.println(errorCode);

  // Start pulse (100ms)
  digitalWrite(TEST_OUT, LOW);
  delay(100);
  digitalWrite(TEST_OUT, HIGH);
  delay(20);

  // Code pulses (10ms each)
  for (int i = 0; i < errorCode; i++) {
    digitalWrite(TEST_OUT, LOW);
    delay(10);
    digitalWrite(TEST_OUT, HIGH);
    delay(10);
  }

  Serial.println("Done");
}
```

**Wiring:**
```
Simulator ESP8266 D1 → Main ESP8266 D5
Simulator GND → Main GND
```

**Pros:** Perfect timing, repeatable
**Cons:** Requires second ESP8266

---

## Verification Checklist

After each error test, verify:

- [ ] Serial monitor shows correct error type
- [ ] HTTP `/health` shows error in `error` object
- [ ] HTTP error `code` matches (1-7)
- [ ] TUI health panel shows error type (not just "ERROR")
- [ ] TUI error history panel shows new error
- [ ] Color coding correct (yellow for 1-2, red for 3-7)
- [ ] Successful dispense clears active error
- [ ] Cleared error stays in history with ✓ indicator
- [ ] Next error increments history (ring buffer works)

## Common Issues

### Error not detected
- Check wiring (D5 connection)
- Verify interrupt attached in serial monitor
- Test with manual jumper wire to confirm decoder works

### Wrong error code detected
- Pulse timing might be off
- Use oscilloscope to measure actual pulses
- Check for electrical noise (add 0.1µF cap)

### Error won't clear
- Verify DispenseManager calls `errorHistory.clearActive()`
- Check serial monitor for "active error cleared" message
- Manually check via HTTP if `cleared` flag set

### Multiple errors at once
- Hopper prioritizes errors (lower code = higher priority)
- Decoder only processes one error sequence at a time
- Clear active error before triggering next one

## Pro Tips

1. **Use serial monitor** - Most reliable way to see what's happening
2. **Test decoder first** - Use Arduino simulator before hopper
3. **Start easy** - Error 6 is safest first test
4. **One at a time** - Don't trigger multiple errors simultaneously
5. **Document results** - Take photos/notes of each test
6. **Power cycle helps** - When in doubt, unplug everything

## Safety Reminders

- ⚠️ Never hot-plug connectors (power off first)
- ⚠️ Don't exceed voltage ratings
- ⚠️ Don't force mechanical parts
- ⚠️ Keep fingers clear of moving parts
- ⚠️ Have a way to emergency stop (power switch nearby)

Happy testing! 🧪
