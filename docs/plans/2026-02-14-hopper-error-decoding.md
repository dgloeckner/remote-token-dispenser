# Hopper Error Decoding Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Decode Azkoyen hopper error pulses (codes 1-7) with interrupt-driven state machine and display error history in TUI

**Architecture:** Three-layer enhancement - (1) ESP8266 ErrorDecoder class with ring buffer, (2) extended /health endpoint, (3) TUI dashboard with error history panel

**Tech Stack:** C++ (Arduino/ESP8266), ArduinoJson, Go (Bubble Tea TUI)

**Design Reference:** `docs/plans/2026-02-14-hopper-error-decoding-design.md`

---

## Task 1: Create error_decoder.h header

**Files:**
- Create: `firmware/dispenser/error_decoder.h`

**Step 1: Write error_decoder.h header file**

```cpp
// firmware/dispenser/error_decoder.h

#ifndef ERROR_DECODER_H
#define ERROR_DECODER_H

#include <Arduino.h>
#include "config.h"

// Error codes from Azkoyen Hopper U-II protocol
// See docs/azkoyen-hopper-protocol.md section 3.5
enum ErrorCode {
  ERROR_NONE = 0,           // No error / Unknown (malformed signal)
  ERROR_COIN_STUCK = 1,     // Coin exit sensor > 65ms
  ERROR_SENSOR_OFF = 2,     // Exit sensor stuck OFF
  ERROR_JAM_PERMANENT = 3,  // Permanent jam detected
  ERROR_MAX_SPAN = 4,       // Multiple spans > max time
  ERROR_MOTOR_FAULT = 5,    // Motor doesn't start
  ERROR_SENSOR_FAULT = 6,   // Exit sensor disconnected
  ERROR_POWER_FAULT = 7     // Power supply out of range
};

// State machine states for pulse decoding
enum DecoderState {
  STATE_IDLE,        // Waiting for error signal (pin HIGH)
  STATE_START_PULSE, // First LOW detected, measuring duration
  STATE_COUNTING,    // Start pulse validated, counting 10ms pulses
  STATE_COMPLETE     // Sequence finished, error code ready
};

class ErrorDecoder {
private:
  DecoderState state;
  volatile unsigned long lastFallTime;  // micros() when pin went LOW
  volatile unsigned long lastPulseTime; // micros() when last pulse ended
  volatile uint8_t pulseCount;          // Number of code pulses counted
  ErrorCode detectedCode;
  bool newErrorReady;

public:
  ErrorDecoder();
  void begin();
  void update();  // Call in main loop - checks timeout, finalizes error code
  void handlePinChange(bool pinState, unsigned long now);  // Called from ISR
  bool hasNewError();
  ErrorCode getErrorCode();
  void reset();
};

// Helper functions for error code conversion
const char* errorCodeToString(ErrorCode code);
const char* errorCodeToDescription(ErrorCode code);

#endif
```

**Step 2: Verify file created**

Check file exists:
```bash
ls -la firmware/dispenser/error_decoder.h
```

Expected: File exists with ~60 lines

**Step 3: Commit**

```bash
git add firmware/dispenser/error_decoder.h
git commit -m "feat(firmware): add ErrorDecoder header with state machine

- Define ErrorCode enum (codes 0-7 per Azkoyen protocol)
- Define DecoderState enum for pulse counting state machine
- ErrorDecoder class interface for interrupt-driven decoding
- Helper functions for error code to string conversion

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 2: Implement error_decoder.cpp

**Files:**
- Create: `firmware/dispenser/error_decoder.cpp`

**Step 1: Write ErrorDecoder constructor and begin()**

```cpp
// firmware/dispenser/error_decoder.cpp

#include "error_decoder.h"

ErrorDecoder::ErrorDecoder()
  : state(STATE_IDLE),
    lastFallTime(0),
    lastPulseTime(0),
    pulseCount(0),
    detectedCode(ERROR_NONE),
    newErrorReady(false) {
}

void ErrorDecoder::begin() {
  state = STATE_IDLE;
  pulseCount = 0;
  lastFallTime = 0;
  lastPulseTime = micros();
  detectedCode = ERROR_NONE;
  newErrorReady = false;

  Serial.println("[ErrorDecoder] Initialized - ready to decode error pulses");
}
```

**Step 2: Write handlePinChange() ISR-safe method**

```cpp
void ErrorDecoder::handlePinChange(bool pinState, unsigned long now) {
  if (pinState == LOW) {
    // FALLING edge - pulse start
    lastFallTime = now;
  } else {
    // RISING edge - pulse end, measure width
    unsigned long width = (now - lastFallTime) / 1000; // convert to ms

    if (state == STATE_IDLE && width >= 90 && width <= 110) {
      // Valid start pulse (100ms ±10%)
      state = STATE_START_PULSE;
      pulseCount = 0;
      lastPulseTime = now;
    } else if (state == STATE_START_PULSE && width >= 8 && width <= 12) {
      // Valid code pulse (10ms ±20%)
      pulseCount++;
      lastPulseTime = now;
    }
  }
}
```

**Step 3: Write update() timeout checker**

```cpp
void ErrorDecoder::update() {
  if (state == STATE_IDLE) return;

  unsigned long elapsed = (micros() - lastPulseTime) / 1000; // ms

  if (elapsed > 200) {
    // Timeout - sequence complete or malformed
    if (state == STATE_START_PULSE && pulseCount >= 1 && pulseCount <= 7) {
      // Valid error code
      detectedCode = (ErrorCode)pulseCount;
    } else {
      // Malformed sequence (wrong pulse count or timeout in wrong state)
      detectedCode = ERROR_NONE; // ERROR_UNKNOWN
    }
    newErrorReady = true;
    state = STATE_IDLE;
  }
}
```

**Step 4: Write helper methods**

```cpp
bool ErrorDecoder::hasNewError() {
  return newErrorReady;
}

ErrorCode ErrorDecoder::getErrorCode() {
  return detectedCode;
}

void ErrorDecoder::reset() {
  newErrorReady = false;
  detectedCode = ERROR_NONE;
}
```

**Step 5: Write error code string conversion functions**

```cpp
const char* errorCodeToString(ErrorCode code) {
  switch (code) {
    case ERROR_COIN_STUCK: return "COIN_STUCK";
    case ERROR_SENSOR_OFF: return "SENSOR_OFF";
    case ERROR_JAM_PERMANENT: return "JAM_PERMANENT";
    case ERROR_MAX_SPAN: return "MAX_SPAN";
    case ERROR_MOTOR_FAULT: return "MOTOR_FAULT";
    case ERROR_SENSOR_FAULT: return "SENSOR_FAULT";
    case ERROR_POWER_FAULT: return "POWER_FAULT";
    default: return "UNKNOWN";
  }
}

const char* errorCodeToDescription(ErrorCode code) {
  switch (code) {
    case ERROR_COIN_STUCK: return "Coin stuck in exit sensor (>65ms)";
    case ERROR_SENSOR_OFF: return "Exit sensor stuck OFF";
    case ERROR_JAM_PERMANENT: return "Permanent jam detected";
    case ERROR_MAX_SPAN: return "Multiple spans exceeded max time";
    case ERROR_MOTOR_FAULT: return "Motor doesn't start";
    case ERROR_SENSOR_FAULT: return "Exit sensor disconnected/faulty";
    case ERROR_POWER_FAULT: return "Power supply out of range";
    default: return "Unknown or malformed error signal";
  }
}
```

**Step 6: Verify compilation**

Compile firmware (Arduino IDE or command line):
```bash
# If using arduino-cli
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 7: Commit**

```bash
git add firmware/dispenser/error_decoder.cpp
git commit -m "feat(firmware): implement ErrorDecoder state machine

- Interrupt-safe handlePinChange() for FALLING/RISING edge detection
- Start pulse validation: 90-110ms (100ms ±10%)
- Code pulse validation: 8-12ms (10ms ±20%)
- 200ms timeout for sequence completion
- Error code conversion helpers (string/description)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 3: Create error_history.h ring buffer

**Files:**
- Create: `firmware/dispenser/error_history.h`

**Step 1: Write error_history.h with ErrorRecord and ErrorHistory**

```cpp
// firmware/dispenser/error_history.h

#ifndef ERROR_HISTORY_H
#define ERROR_HISTORY_H

#include <Arduino.h>
#include "error_decoder.h"

// Single error record in ring buffer
struct ErrorRecord {
  ErrorCode code;
  unsigned long timestamp;  // millis() when detected
  bool cleared;             // false = active, true = cleared by successful dispense
};

// Ring buffer for last 5 errors
class ErrorHistory {
private:
  ErrorRecord buffer[5];
  uint8_t writeIndex;

public:
  ErrorHistory();
  void addError(ErrorCode code);
  ErrorRecord* getActive();  // Returns first non-cleared error (newest first), or nullptr
  void clearActive();        // Marks active error as cleared
  void getAll(ErrorRecord* output, int& count);  // Returns all non-NONE errors (newest first)
};

#endif
```

**Step 2: Write error_history.cpp implementation**

Create: `firmware/dispenser/error_history.cpp`

```cpp
// firmware/dispenser/error_history.cpp

#include "error_history.h"

ErrorHistory::ErrorHistory() : writeIndex(0) {
  // Initialize buffer with ERROR_NONE
  for (int i = 0; i < 5; i++) {
    buffer[i] = {ERROR_NONE, 0, true};
  }
}

void ErrorHistory::addError(ErrorCode code) {
  buffer[writeIndex] = {code, millis(), false};
  writeIndex = (writeIndex + 1) % 5;

  Serial.print("[ErrorHistory] Added error: ");
  Serial.print(errorCodeToString(code));
  Serial.print(" at timestamp ");
  Serial.println(millis());
}

ErrorRecord* ErrorHistory::getActive() {
  // Search newest to oldest for first non-cleared error
  for (int i = 0; i < 5; i++) {
    int idx = (writeIndex - 1 - i + 5) % 5;
    if (buffer[idx].code != ERROR_NONE && !buffer[idx].cleared) {
      return &buffer[idx];
    }
  }
  return nullptr;
}

void ErrorHistory::clearActive() {
  ErrorRecord* active = getActive();
  if (active) {
    active->cleared = true;
    Serial.print("[ErrorHistory] Cleared active error: ");
    Serial.println(errorCodeToString(active->code));
  }
}

void ErrorHistory::getAll(ErrorRecord* output, int& count) {
  // Return all non-NONE errors, newest first
  count = 0;
  for (int i = 0; i < 5; i++) {
    int idx = (writeIndex - 1 - i + 5) % 5;
    if (buffer[idx].code != ERROR_NONE) {
      output[count++] = buffer[idx];
    }
  }
}
```

**Step 3: Verify compilation**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 4: Commit**

```bash
git add firmware/dispenser/error_history.h firmware/dispenser/error_history.cpp
git commit -m "feat(firmware): add ErrorHistory ring buffer

- 5-error ring buffer with timestamp and cleared flag
- addError() with automatic wrap-around
- getActive() searches newest-first for non-cleared error
- clearActive() marks active error as cleared
- getAll() returns all errors for history display

Memory: 5 × 6 bytes = 30 bytes

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 4: Integrate ErrorDecoder into hopper_control

**Files:**
- Modify: `firmware/dispenser/hopper_control.h`
- Modify: `firmware/dispenser/hopper_control.cpp`

**Step 1: Add includes and instance to hopper_control.h**

Add to `firmware/dispenser/hopper_control.h` after existing includes:

```cpp
#include "error_decoder.h"
#include "error_history.h"
```

Add to HopperControl class (public section):

```cpp
  // Error handling
  ErrorDecoder errorDecoder;
  ErrorHistory errorHistory;
  void updateErrorDecoder();  // Call in main loop
```

**Step 2: Add ISR wrapper to hopper_control.cpp**

Add global ISR function at top of `firmware/dispenser/hopper_control.cpp` (after includes):

```cpp
// Global instance pointer for ISR
static HopperControl* hopperControlInstance = nullptr;

// ISR for error pin change detection
void IRAM_ATTR handleErrorPinChange() {
  if (hopperControlInstance) {
    bool pinState = digitalRead(ERROR_SIGNAL_PIN);
    unsigned long now = micros();
    hopperControlInstance->errorDecoder.handlePinChange(pinState, now);
  }
}
```

**Step 3: Update HopperControl::begin() to initialize decoder and attach interrupt**

Add to `HopperControl::begin()` after existing pin setup:

```cpp
  // Initialize error decoder
  errorDecoder.begin();

  // Set global instance for ISR
  hopperControlInstance = this;

  // Attach interrupt for error signal (CHANGE edge - both FALLING and RISING)
  attachInterrupt(digitalPinToInterrupt(ERROR_SIGNAL_PIN),
                  handleErrorPinChange, CHANGE);
  Serial.println("[HopperControl] Interrupt attached to ERROR_SIGNAL_PIN (CHANGE edge)");
```

**Step 4: Add updateErrorDecoder() method to hopper_control.cpp**

Add after existing methods:

```cpp
void HopperControl::updateErrorDecoder() {
  errorDecoder.update();

  if (errorDecoder.hasNewError()) {
    ErrorCode code = errorDecoder.getErrorCode();
    errorHistory.addError(code);
    errorDecoder.reset();

    Serial.print("[HopperControl] Error detected: ");
    Serial.print(errorCodeToString(code));
    Serial.print(" - ");
    Serial.println(errorCodeToDescription(code));
  }
}
```

**Step 5: Verify compilation**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 6: Commit**

```bash
git add firmware/dispenser/hopper_control.h firmware/dispenser/hopper_control.cpp
git commit -m "feat(firmware): integrate ErrorDecoder into HopperControl

- Add ErrorDecoder and ErrorHistory instances to HopperControl
- Attach CHANGE interrupt to ERROR_SIGNAL_PIN (D5/GPIO14)
- ISR calls errorDecoder.handlePinChange()
- updateErrorDecoder() polls for new errors and adds to history

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 5: Call updateErrorDecoder() from main loop

**Files:**
- Modify: `firmware/dispenser/dispenser.ino`

**Step 1: Add error decoder update to main loop**

In `loop()` function of `firmware/dispenser/dispenser.ino`, add after existing hopper control logic:

```cpp
void loop() {
  // ... existing WiFi/server handling ...

  // Update error decoder (check timeouts, process new errors)
  hopperControl.updateErrorDecoder();

  // ... existing dispense manager update ...
}
```

**Step 2: Verify compilation**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 3: Commit**

```bash
git add firmware/dispenser/dispenser.ino
git commit -m "feat(firmware): call updateErrorDecoder in main loop

- Polls error decoder for timeout and new errors
- Adds detected errors to history ring buffer

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 6: Clear errors on successful dispense

**Files:**
- Modify: `firmware/dispenser/dispense_manager.cpp`
- Modify: `firmware/dispenser/dispense_manager.h`

**Step 1: Add HopperControl reference to DispenseManager**

In `firmware/dispenser/dispense_manager.h`, update constructor:

```cpp
class DispenseManager {
private:
  HopperControl& hopperControl;  // Add reference
  // ... existing fields ...

public:
  DispenseManager(HopperControl& hopper);  // Update constructor signature
  // ... existing methods ...
};
```

**Step 2: Update DispenseManager constructor in dispense_manager.cpp**

```cpp
DispenseManager::DispenseManager(HopperControl& hopper)
  : hopperControl(hopper) {
  // ... existing initialization ...
}
```

**Step 3: Clear active error on successful dispense**

In `DispenseManager::update()`, find the state transition to `STATE_DONE` and add error clearing:

```cpp
if (state == STATE_DISPENSING && dispensed >= quantity) {
  state = STATE_DONE;

  // Clear active error on successful completion (self-healing)
  hopperControl.errorHistory.clearActive();

  successful++;
  Serial.println("[DispenseManager] Dispense complete - active error cleared");
}
```

**Step 4: Update DispenseManager instantiation in dispenser.ino**

In `firmware/dispenser/dispenser.ino`, update DispenseManager creation:

```cpp
DispenseManager dispenseManager(hopperControl);  // Pass hopperControl reference
```

**Step 5: Verify compilation**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 6: Commit**

```bash
git add firmware/dispenser/dispense_manager.h firmware/dispenser/dispense_manager.cpp firmware/dispenser/dispenser.ino
git commit -m "feat(firmware): clear active error on successful dispense

- DispenseManager takes HopperControl reference
- Call errorHistory.clearActive() on STATE_DONE transition
- Self-healing: successful dispense proves recovery

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 7: Extend /health endpoint with error fields

**Files:**
- Modify: `firmware/dispenser/http_server.cpp`

**Step 1: Add error object to health response**

In `HttpServer::handleHealth()`, after existing fields and before `serializeJson()`:

```cpp
void HttpServer::handleHealth(AsyncWebServerRequest *request) {
  JsonDocument doc;

  // ... existing fields (status, uptime, firmware, wifi, dispenser, gpio, metrics) ...

  // Add error information
  ErrorRecord* active = hopperControl.errorHistory.getActive();
  if (active) {
    JsonObject err = doc.createNestedObject("error");
    err["active"] = true;
    err["code"] = (int)active->code;
    err["type"] = errorCodeToString(active->code);
    err["timestamp"] = active->timestamp;
    err["description"] = errorCodeToDescription(active->code);
  } else {
    JsonObject err = doc.createNestedObject("error");
    err["active"] = false;
  }

  // ... continue with serializeJson() ...
}
```

**Step 2: Add error history array to health response**

Add after error object (still in `handleHealth()`):

```cpp
  // Add error history (last 5 errors)
  JsonArray history = doc.createNestedArray("error_history");
  ErrorRecord records[5];
  int count;
  hopperControl.errorHistory.getAll(records, count);

  for (int i = 0; i < count; i++) {
    JsonObject e = history.createNestedObject();
    e["code"] = (int)records[i].code;
    e["type"] = errorCodeToString(records[i].code);
    e["timestamp"] = records[i].timestamp;
    e["cleared"] = records[i].cleared;
  }
```

**Step 3: Verify compilation**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Compiles without errors

**Step 4: Test with curl (manual)**

After uploading firmware, test endpoint:

```bash
curl http://192.168.4.20/health | jq '.error, .error_history'
```

Expected output (no errors):
```json
{
  "active": false
}
[]
```

**Step 5: Commit**

```bash
git add firmware/dispenser/http_server.cpp
git commit -m "feat(firmware): extend /health endpoint with error fields

- Add 'error' object with active/code/type/timestamp/description
- Add 'error_history' array with last 5 errors
- Backward compatible (new fields optional)

Example response:
{
  \"error\": {\"active\": false},
  \"error_history\": []
}

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 8: Update TUI client structs

**Files:**
- Modify: `dispenser-client-tui/client.go`

**Step 1: Add ErrorInfo struct to client.go**

Add after existing type definitions in `dispenser-client-tui/client.go`:

```go
type ErrorInfo struct {
	Active      bool   `json:"active"`
	Code        int    `json:"code,omitempty"`
	Type        string `json:"type,omitempty"`
	Timestamp   int64  `json:"timestamp,omitempty"`
	Description string `json:"description,omitempty"`
}

type ErrorRecord struct {
	Code      int    `json:"code"`
	Type      string `json:"type"`
	Timestamp int64  `json:"timestamp"`
	Cleared   bool   `json:"cleared"`
}
```

**Step 2: Add Error fields to HealthResponse**

Update `HealthResponse` struct to include:

```go
type HealthResponse struct {
	Status       string        `json:"status"`
	Uptime       int           `json:"uptime"`
	Firmware     string        `json:"firmware"`
	WiFi         *WiFiInfo     `json:"wifi,omitempty"`
	Dispenser    string        `json:"dispenser"`
	GPIO         *GPIOInfo     `json:"gpio,omitempty"`
	Metrics      Metrics       `json:"metrics"`
	ActiveTx     *ActiveTxInfo `json:"active_tx,omitempty"`
	Error        *ErrorInfo    `json:"error,omitempty"`          // NEW
	ErrorHistory []ErrorRecord `json:"error_history,omitempty"`  // NEW
}
```

**Step 3: Verify Go build**

```bash
cd dispenser-client-tui
go build
```

Expected: Builds without errors

**Step 4: Commit**

```bash
git add dispenser-client-tui/client.go
git commit -m "feat(tui): add Error and ErrorHistory to client structs

- Add ErrorInfo struct (active, code, type, timestamp, description)
- Add ErrorRecord struct (code, type, timestamp, cleared)
- Update HealthResponse to include error and error_history fields

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 9: Update TUI health panel with error display

**Files:**
- Modify: `dispenser-client-tui/views.go`

**Step 1: Update renderHealthPanel to show decoded errors**

Find the hopper status section in `renderHealthPanel()` and replace with:

```go
	// Hopper status with decoded error
	if m.health.Error != nil && m.health.Error.Active {
		// Active error - show type and severity
		errStyle := statusError
		if m.health.Error.Code <= 2 {
			errStyle = statusWarning // Sensor issues = yellow
		}
		lines = append(lines, labelStyle.Render("Hopper:")+
			" "+errStyle.Render(fmt.Sprintf("⚠ %s", m.health.Error.Type)))
	} else if m.health.GPIO != nil && m.health.GPIO.HopperLow.Active {
		lines = append(lines, labelStyle.Render("Hopper:")+" "+statusOK.Render("● OK"))
	} else {
		lines = append(lines, labelStyle.Render("Hopper:")+" "+statusWarning.Render("⚠ EMPTY"))
	}
```

**Step 2: Verify Go build**

```bash
cd dispenser-client-tui
go build
```

Expected: Builds without errors

**Step 3: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): display decoded error in health panel

- Show error type (e.g., 'JAM_PERMANENT') instead of generic 'ERROR'
- Color-code by severity: yellow for codes 1-2, red for 3-7
- Replace binary error_signal.active with decoded error info

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 10: Add error history panel to TUI

**Files:**
- Modify: `dispenser-client-tui/views.go`

**Step 1: Add formatAge helper function**

Add to `dispenser-client-tui/views.go` after existing helper functions:

```go
func formatAge(seconds int64) string {
	if seconds < 60 {
		return fmt.Sprintf("%ds ago", seconds)
	}
	if seconds < 3600 {
		return fmt.Sprintf("%dm ago", seconds/60)
	}
	return fmt.Sprintf("%dh ago", seconds/3600)
}
```

**Step 2: Add renderErrorHistoryPanel function**

Add new function to `dispenser-client-tui/views.go`:

```go
func (m Model) renderErrorHistoryPanel(w int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("🚨 Recent Errors (Last 5)"))
	lines = append(lines, "")

	if m.health == nil || m.health.ErrorHistory == nil || len(m.health.ErrorHistory) == 0 {
		lines = append(lines, statusOK.Render("  ✓ No errors recorded"))
	} else {
		for _, err := range m.health.ErrorHistory {
			// Status indicator
			status := "✓"
			style := statusMuted
			if !err.Cleared {
				status = "⚠"
				if err.Code >= 3 {
					style = statusError // Critical errors = red
				} else {
					style = statusWarning // Sensor errors = yellow
				}
			}

			// Format age
			age := formatAge(time.Now().Unix() - err.Timestamp/1000)

			// Error type
			typeStr := style.Render(fmt.Sprintf("%-15s", err.Type))

			lines = append(lines, fmt.Sprintf("  %s %s %s",
				status, typeStr, statusMuted.Render(age)))
		}
	}

	content := strings.Join(lines, "\n")
	return panelStyle.Width(w).Render(content)
}
```

**Step 3: Add error history panel to dashboard layout**

Update `renderDashboard()` to include error history panel after latency panel:

```go
func (m Model) renderDashboard(w, h int) string {
	var b strings.Builder

	// Top: health + metrics
	leftCol := m.renderHealthPanel(w/2 - 2)
	rightCol := m.renderMetricsPanel(w/2 - 2)
	topRow := lipgloss.JoinHorizontal(lipgloss.Top, leftCol, "  ", rightCol)
	b.WriteString(topRow)
	b.WriteString("\n")

	// Latency sparkline
	b.WriteString(m.renderLatencyPanel(w - 4))
	b.WriteString("\n")

	// NEW: Error history panel
	b.WriteString(m.renderErrorHistoryPanel(w - 4))
	b.WriteString("\n")

	// GPIO debug (if enabled)
	if m.debugMode {
		b.WriteString(m.renderGPIODebugPanel(w - 4))
		b.WriteString("\n")
	}

	// Recent log
	b.WriteString(m.renderRecentLog(w-4, 8))

	return b.String()
}
```

**Step 4: Verify Go build**

```bash
cd dispenser-client-tui
go build
```

Expected: Builds without errors

**Step 5: Manual test with TUI**

Run TUI and verify error history panel appears:

```bash
cd dispenser-client-tui
./token-dispenser-tui
```

Expected: New "🚨 Recent Errors (Last 5)" panel with "✓ No errors recorded"

**Step 6: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): add error history panel to dashboard

- New renderErrorHistoryPanel showing last 5 errors
- formatAge helper (seconds -> '5m ago' format)
- Color coding: red for critical (codes 3-7), yellow for sensor (1-2)
- Status indicator: ⚠ for active, ✓ for cleared
- Positioned between latency and GPIO debug panels

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 11: Build and upload firmware

**Files:**
- Modify: None (build/upload step)

**Step 1: Clean build firmware**

```bash
cd firmware/dispenser
arduino-cli compile --clean --fqbn esp8266:esp8266:d1_mini .
```

Expected: Clean build succeeds

**Step 2: Upload to ESP8266**

Connect ESP8266 via USB and upload:

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:d1_mini .
```

(Adjust port as needed: `/dev/ttyUSB0`, `/dev/cu.usbserial-*`, etc.)

Expected: Upload succeeds

**Step 3: Open Serial Monitor**

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Expected output includes:
```
[ErrorDecoder] Initialized - ready to decode error pulses
[HopperControl] Interrupt attached to ERROR_SIGNAL_PIN (CHANGE edge)
```

**Step 4: Verify /health endpoint**

```bash
curl http://192.168.4.20/health | jq '.error, .error_history'
```

Expected:
```json
{
  "active": false
}
[]
```

**Step 5: Document upload (no commit needed)**

This is a deployment step, not code change.

---

## Task 12: Manual testing - simulate error pulse

**Files:**
- Modify: None (hardware testing)

**Step 1: Test error detection with jumper wire**

Hardware setup:
1. Connect jumper wire to D5 (GPIO14) on ESP8266
2. Monitor Serial output
3. Manually create pulse sequence:
   - Touch wire to GND for ~100ms (start pulse)
   - Touch wire to GND 3 times for ~10ms each (code pulses)
   - Wait 200ms

Expected Serial output:
```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
[ErrorHistory] Added error: JAM_PERMANENT at timestamp 12345
```

**Step 2: Verify /health endpoint shows error**

```bash
curl http://192.168.4.20/health | jq '.error'
```

Expected:
```json
{
  "active": true,
  "code": 3,
  "type": "JAM_PERMANENT",
  "timestamp": 12345,
  "description": "Permanent jam detected"
}
```

**Step 3: Check error history**

```bash
curl http://192.168.4.20/health | jq '.error_history'
```

Expected:
```json
[
  {
    "code": 3,
    "type": "JAM_PERMANENT",
    "timestamp": 12345,
    "cleared": false
  }
]
```

**Step 4: Test TUI display**

Run TUI and verify:
- Health panel shows "Hopper: ⚠ JAM_PERMANENT" in red
- Error history panel shows "⚠ JAM_PERMANENT   0s ago" in red

```bash
cd dispenser-client-tui
./token-dispenser-tui
```

**Step 5: Document test results**

Record findings in testing notes (informal, no commit).

---

## Task 13: Manual testing - error clearing on dispense

**Files:**
- Modify: None (hardware testing)

**Step 1: Ensure active error exists**

From previous test, verify error is still active:

```bash
curl http://192.168.4.20/health | jq '.error.active'
```

Expected: `true`

**Step 2: Trigger successful dispense**

Manually trigger hopper or use test endpoint to complete dispense:

```bash
curl -X POST http://192.168.4.20/dispense \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-api-key" \
  -d '{"tx_id": "test123", "quantity": 1}'
```

Wait for dispense to complete (watch Serial Monitor).

Expected Serial output:
```
[DispenseManager] Dispense complete - active error cleared
[ErrorHistory] Cleared active error: JAM_PERMANENT
```

**Step 3: Verify error cleared in /health**

```bash
curl http://192.168.4.20/health | jq '.error.active'
```

Expected: `false`

**Step 4: Verify error history shows cleared flag**

```bash
curl http://192.168.4.20/health | jq '.error_history[0].cleared'
```

Expected: `true`

**Step 5: Check TUI display**

TUI should show:
- Health panel: "Hopper: ● OK" (or "⚠ EMPTY" if hopper low)
- Error history panel: "✓ JAM_PERMANENT   2m ago" (muted gray)

**Step 6: Document test results**

Self-healing confirmed.

---

## Task 14: Manual testing - ring buffer wrap

**Files:**
- Modify: None (hardware testing)

**Step 1: Generate 7 errors**

Manually trigger error signal 7 times (simulate different error codes):

For each code 1-7:
```
1. Touch D5 to GND for 100ms (start)
2. Touch D5 to GND N times for 10ms (N = error code)
3. Wait 200ms between sequences
```

Watch Serial Monitor for 7 error detections.

**Step 2: Verify only last 5 errors stored**

```bash
curl http://192.168.4.20/health | jq '.error_history | length'
```

Expected: `5` (oldest 2 errors dropped)

**Step 3: Verify newest-first order**

```bash
curl http://192.168.4.20/health | jq '.error_history[].code'
```

Expected: Most recent 5 error codes in reverse chronological order

**Step 4: Check TUI display**

Error history panel should show max 5 errors, newest at top.

**Step 5: Document test results**

Ring buffer wrapping confirmed.

---

## Task 15: Update firmware version and final commit

**Files:**
- Modify: `firmware/dispenser/config.h`

**Step 1: Update firmware version string**

In `firmware/dispenser/config.h`, change:

```cpp
#define FIRMWARE_VERSION   "1.0.0-DEBUG"
```

To:

```cpp
#define FIRMWARE_VERSION   "1.1.0-DEBUG-error-decoding"
```

**Step 2: Rebuild and verify**

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

Expected: Builds successfully

**Step 3: Test /health reports new version**

After upload:

```bash
curl http://192.168.4.20/health | jq '.firmware'
```

Expected: `"1.1.0-DEBUG-error-decoding"`

**Step 4: Commit**

```bash
git add firmware/dispenser/config.h
git commit -m "chore(firmware): bump version to 1.1.0-DEBUG-error-decoding

Feature complete:
- Error decoder with interrupt-driven state machine
- 5-error ring buffer with self-healing
- Extended /health endpoint
- TUI error history panel

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 16: Update documentation

**Files:**
- Modify: `firmware/README.md` or create `docs/error-decoding-usage.md`

**Step 1: Document error decoding feature**

Create `docs/error-decoding-usage.md`:

```markdown
# Error Decoding Usage Guide

## Overview

The hopper error decoding feature decodes Azkoyen Hopper U-II error signals and maintains a history of the last 5 errors.

## Error Codes

| Code | Type | Description | Severity |
|------|------|-------------|----------|
| 0 | UNKNOWN | Malformed error signal | Low |
| 1 | COIN_STUCK | Coin stuck in exit sensor (>65ms) | Medium |
| 2 | SENSOR_OFF | Exit sensor stuck OFF | Medium |
| 3 | JAM_PERMANENT | Permanent jam detected | High |
| 4 | MAX_SPAN | Multiple spans exceeded max time | High |
| 5 | MOTOR_FAULT | Motor doesn't start | High |
| 6 | SENSOR_FAULT | Exit sensor disconnected/faulty | High |
| 7 | POWER_FAULT | Power supply out of range | High |

## API Usage

### GET /health

Returns error information:

```json
{
  "error": {
    "active": true,
    "code": 3,
    "type": "JAM_PERMANENT",
    "timestamp": 123456,
    "description": "Permanent jam detected"
  },
  "error_history": [
    {
      "code": 3,
      "type": "JAM_PERMANENT",
      "timestamp": 123456,
      "cleared": false
    }
  ]
}
```

## Self-Healing

Active errors are automatically cleared when a successful dispense completes. The error remains in history with `cleared: true`.

## TUI Display

- **Health Panel**: Shows active error type with color coding (yellow=sensor, red=critical)
- **Error History Panel**: Shows last 5 errors with age and cleared status

## Serial Monitor

Error events are logged:

```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
[ErrorHistory] Added error: JAM_PERMANENT at timestamp 12345
[ErrorHistory] Cleared active error: JAM_PERMANENT
```

## Testing

Simulate errors by creating pulse sequences on D5 (GPIO14):
1. 100ms LOW (start pulse)
2. N × 10ms LOW (code pulses, N = 1-7)
3. 200ms timeout

## Troubleshooting

- **No errors detected**: Check ERROR_SIGNAL_PIN wiring (D5 to hopper pin 8 via optocoupler)
- **Wrong error codes**: Verify pulse timing (100ms start, 10ms codes)
- **Errors don't clear**: Check dispense completion logic in DispenseManager
```

**Step 2: Commit documentation**

```bash
git add docs/error-decoding-usage.md
git commit -m "docs: add error decoding usage guide

- Error code table with severity
- API response format
- Self-healing behavior
- TUI display reference
- Testing/troubleshooting guide

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Testing Validation Checklist

After completing all tasks, verify:

- [ ] Error codes 1-7 decode correctly from pulse sequences
- [ ] ERROR_UNKNOWN (0) reported for malformed signals (wrong pulse count/timing)
- [ ] Ring buffer wraps correctly at 5 entries (test with 7+ errors)
- [ ] Active error clears on successful dispense
- [ ] Error history persists in ring buffer (cleared flag set)
- [ ] `/health` endpoint returns `error` and `error_history` fields
- [ ] TUI health panel shows decoded error type with color coding
- [ ] TUI error history panel displays last 5 errors with age formatting
- [ ] Color coding works (yellow for codes 1-2, red for 3-7, muted for cleared)
- [ ] GPIO debug panel unchanged (backward compatibility)
- [ ] No memory leaks or crashes during extended operation
- [ ] Serial logging provides clear debug information

## Completion

When all tasks done and validation passes:

```bash
git log --oneline | head -20
```

Review commits for completeness. Feature ready for PR or merge to main.

---

**Plan Status:** Ready for execution
**Created:** 2026-02-14
**Design Reference:** `docs/plans/2026-02-14-hopper-error-decoding-design.md`
