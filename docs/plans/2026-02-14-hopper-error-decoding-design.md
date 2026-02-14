# Hopper Error Decoding - Detailed Error Reporting Design

**Date:** 2026-02-14
**Status:** Approved for implementation
**Target:** Feature branch implementation

## Overview

Implement detailed hopper error reporting by decoding the Azkoyen Hopper U-II error signal pulse protocol. This enhances the current binary error detection (active/inactive) to identify specific error types (jam, motor fault, power issue, etc.) and maintain error history for diagnostics.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Error clearing** | On successful dispense | Self-healing - proves recovery |
| **Error persistence** | Ring buffer | Balance diagnostics vs memory |
| **Buffer size** | 5 errors | ~50 bytes RAM, adequate history |
| **Blocking behavior** | Allow dispense attempts | Enable self-recovery |
| **TUI visibility** | Dashboard + history panel | Good visibility without clutter |
| **Pulse counting** | Interrupt-driven state machine | Accurate per protocol spec |
| **Invalid signals** | Report as ERROR_UNKNOWN (0) | Helps diagnose wiring issues |

## Architecture

### Three-Layer Enhancement

**1. Firmware (ESP8266)**
- New `ErrorDecoder` class with interrupt-driven state machine
- Ring buffer storing last 5 errors (type, timestamp, cleared status)
- Error state cleared on successful dispense completion
- Memory footprint: ~50 bytes (negligible impact)

**2. Protocol Extension**
- Extend `/health` endpoint response with error fields
- Maintain backward compatibility (new fields optional)

**3. TUI (Go/Bubble Tea)**
- Update `HealthResponse` struct with error fields
- Add "🚨 Recent Errors" panel to dashboard
- Update health panel to show decoded error type
- Color-coded error severity (yellow=recoverable, red=critical)

### Design Principle

**Non-blocking and self-healing:** Errors don't prevent dispense attempts. Successful dispense clears active error and proves recovery.

## Firmware Implementation

### Error Codes (Azkoyen Protocol)

```cpp
enum ErrorCode {
  ERROR_NONE = 0,           // No error / Unknown
  ERROR_COIN_STUCK = 1,     // Coin exit sensor > 65ms
  ERROR_SENSOR_OFF = 2,     // Exit sensor stuck OFF
  ERROR_JAM_PERMANENT = 3,  // Permanent jam detected
  ERROR_MAX_SPAN = 4,       // Multiple spans > max time
  ERROR_MOTOR_FAULT = 5,    // Motor doesn't start
  ERROR_SENSOR_FAULT = 6,   // Exit sensor disconnected
  ERROR_POWER_FAULT = 7     // Power supply out of range
};
```

**Special Case:** `ERROR_NONE` (0) also used for `ERROR_UNKNOWN` (malformed pulse sequences).

### State Machine

**States:**
1. **IDLE** - Waiting for error signal (pin HIGH)
2. **START_PULSE** - First LOW detected, measuring duration
3. **COUNTING** - Start pulse validated (90-110ms), counting 10ms pulses
4. **COMPLETE** - Sequence finished, error code ready

**Interrupt Handler (ISR):**

Attach CHANGE interrupt to D5 (GPIO14):

```cpp
void IRAM_ATTR onErrorPinChange() {
  bool pinState = digitalRead(ERROR_SIGNAL_PIN);
  unsigned long now = micros();

  if (pinState == LOW) {
    // FALLING edge - pulse start
    lastFallTime = now;
  } else {
    // RISING edge - pulse end
    unsigned long width = (now - lastFallTime) / 1000; // ms

    if (state == IDLE && width >= 90 && width <= 110) {
      // Valid start pulse (100ms ±10%)
      state = START_PULSE;
      pulseCount = 0;
      lastPulseTime = now;
    } else if (state == START_PULSE && width >= 8 && width <= 12) {
      // Valid code pulse (10ms ±20%)
      pulseCount++;
      lastPulseTime = now;
    }
  }
}
```

**Main Loop Processing:**

```cpp
void ErrorDecoder::update() {
  if (state == IDLE) return;

  unsigned long elapsed = (micros() - lastPulseTime) / 1000;

  if (elapsed > 200) {
    // Timeout - sequence complete
    if (state == START_PULSE && pulseCount >= 1 && pulseCount <= 7) {
      // Valid error code
      detectedCode = (ErrorCode)pulseCount;
    } else {
      // Malformed sequence
      detectedCode = ERROR_UNKNOWN;
    }
    newErrorReady = true;
    state = IDLE;
  }
}
```

**Timing Tolerances:**
- Start pulse: 90-110ms (protocol spec: 100ms)
- Code pulses: 8-12ms (protocol spec: 10ms ±20% tolerance)
- Sequence timeout: 200ms (generous to handle slow edges)

### Data Structures

**Error Record:**

```cpp
struct ErrorRecord {
  ErrorCode code;
  unsigned long timestamp;  // millis() when detected
  bool cleared;             // false = active, true = cleared by successful dispense
};
```

**Ring Buffer:**

```cpp
class ErrorHistory {
private:
  ErrorRecord buffer[5];
  uint8_t writeIndex = 0;

public:
  void addError(ErrorCode code) {
    buffer[writeIndex] = {code, millis(), false};
    writeIndex = (writeIndex + 1) % 5;
  }

  ErrorRecord* getActive() {
    // Search newest to oldest for first non-cleared error
    for (int i = 0; i < 5; i++) {
      int idx = (writeIndex - 1 - i + 5) % 5;
      if (buffer[idx].code != ERROR_NONE && !buffer[idx].cleared) {
        return &buffer[idx];
      }
    }
    return nullptr;
  }

  void clearActive() {
    ErrorRecord* active = getActive();
    if (active) {
      active->cleared = true;
    }
  }

  void getAll(ErrorRecord* output, int& count) {
    // Return all non-NONE errors, newest first
    count = 0;
    for (int i = 0; i < 5; i++) {
      int idx = (writeIndex - 1 - i + 5) % 5;
      if (buffer[idx].code != ERROR_NONE) {
        output[count++] = buffer[idx];
      }
    }
  }
};
```

**Memory Usage:**
- 5 × (1 byte code + 4 bytes timestamp + 1 byte flag) = 30 bytes
- Decoder state variables: ~20 bytes
- **Total: ~50 bytes** (0.06% of ESP8266 80KB RAM)

### Integration with Dispense Flow

**Error Detection (main loop):**

```cpp
void loop() {
  errorDecoder.update();  // Process state machine, check timeouts

  if (errorDecoder.hasNewError()) {
    ErrorCode code = errorDecoder.getErrorCode();
    errorHistory.addError(code);
    errorDecoder.reset();

    Serial.print("[ERROR] Hopper error detected: ");
    Serial.println(errorCodeToString(code));
  }

  // ... existing dispense logic ...
}
```

**Clearing on Successful Dispense:**

In `DispenseManager::update()`, when transitioning to `STATE_DONE`:

```cpp
if (state == STATE_DISPENSING && dispensed >= quantity) {
  state = STATE_DONE;

  // Clear active error on successful completion
  errorHistory.clearActive();

  // Update metrics
  successful++;

  Serial.println("[DISPENSE] Success - active error cleared");
}
```

**Health Endpoint Response:**

```cpp
void HttpServer::handleHealth(AsyncWebServerRequest *request) {
  JsonDocument doc;

  // ... existing fields (status, uptime, firmware, wifi, dispenser, gpio, metrics) ...

  // Add error information
  ErrorRecord* active = errorHistory.getActive();
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

  // Add error history
  JsonArray history = doc.createNestedArray("error_history");
  ErrorRecord records[5];
  int count;
  errorHistory.getAll(records, count);

  for (int i = 0; i < count; i++) {
    JsonObject e = history.createNestedObject();
    e["code"] = (int)records[i].code;
    e["type"] = errorCodeToString(records[i].code);
    e["timestamp"] = records[i].timestamp;
    e["cleared"] = records[i].cleared;
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}
```

**Helper Functions:**

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

## Protocol Extension

### Health Endpoint Response Schema

**New fields added to existing `/health` response:**

```json
{
  "status": "ok",
  "uptime": 3600,
  "firmware": "1.0.0-DEBUG",
  "wifi": { ... },
  "dispenser": "idle",
  "gpio": { ... },
  "metrics": { ... },

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
    },
    {
      "code": 1,
      "type": "COIN_STUCK",
      "timestamp": 120000,
      "cleared": true
    }
  ]
}
```

**Backward Compatibility:**
- Existing clients ignore new `error` and `error_history` fields
- GPIO debug info (`gpio.error_signal`) unchanged - still shows raw pin state

## TUI Implementation

### Client Struct Updates (client.go)

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

type ErrorInfo struct {
  Active      bool   `json:"active"`
  Code        int    `json:"code"`
  Type        string `json:"type"`
  Timestamp   int64  `json:"timestamp"`
  Description string `json:"description"`
}

type ErrorRecord struct {
  Code      int    `json:"code"`
  Type      string `json:"type"`
  Timestamp int64  `json:"timestamp"`
  Cleared   bool   `json:"cleared"`
}
```

### Dashboard Display Updates (views.go)

#### 1. Health Panel Enhancement

Replace generic error signal display with decoded error:

```go
func (m Model) renderHealthPanel(w int) string {
  var lines []string
  lines = append(lines, sectionHeader.Render("⚡ Health"))
  lines = append(lines, "")

  if m.health != nil {
    // ... existing status, dispenser, uptime, firmware, wifi ...

    // Hopper status with error
    if m.health.Error != nil && m.health.Error.Active {
      // Active error - show type and severity
      errStyle := statusError
      if m.health.Error.Code <= 2 {
        errStyle = statusWarning  // Sensor issues = yellow
      }
      lines = append(lines, labelStyle.Render("Hopper:")+
        " "+errStyle.Render(fmt.Sprintf("⚠ %s", m.health.Error.Type)))
    } else if m.health.GPIO != nil && m.health.GPIO.HopperLow.Active {
      lines = append(lines, labelStyle.Render("Hopper:")+" "+statusOK.Render("● OK"))
    } else {
      lines = append(lines, labelStyle.Render("Hopper:")+" "+statusWarning.Render("⚠ EMPTY"))
    }

    // ... rest of health panel ...
  }

  content := strings.Join(lines, "\n")
  return panelStyle.Width(w).Render(content)
}
```

#### 2. New Error History Panel

Add to dashboard layout (below latency panel):

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
          style = statusError  // Critical errors = red
        } else {
          style = statusWarning  // Sensor errors = yellow
        }
      }

      // Format age
      age := formatAge(time.Now().Unix() - err.Timestamp/1000)

      // Error type and description
      typeStr := style.Render(fmt.Sprintf("%-15s", err.Type))

      lines = append(lines, fmt.Sprintf("  %s %s %s",
        status, typeStr, statusMuted.Render(age)))
    }
  }

  content := strings.Join(lines, "\n")
  return panelStyle.Width(w).Render(content)
}
```

**Helper Function:**

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

#### 3. Dashboard Layout Update

Insert error history panel between latency and recent log:

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

### Color Coding

**Error Severity:**
- **Yellow (warning):** Codes 1-2 (sensor issues - recoverable)
- **Red (error):** Codes 3-7 (jam, motor, power - critical)

**Cleared Status:**
- **Muted gray:** Cleared errors (historical)
- **Colored:** Active errors

## Testing & Validation

### Manual Testing Strategy

**Firmware Testing (Serial Monitor):**

1. **Error Signal Simulation:**
   - Use function generator or Arduino to generate pulse sequences
   - Test each error code (1-7) with correct timing:
     - 100ms start pulse + N×10ms pulses
   - Verify Serial output shows correct error type

2. **Malformed Signal Testing:**
   - Send wrong pulse count (0, 8, 9)
   - Send wrong pulse widths (5ms, 20ms)
   - Verify reports as ERROR_UNKNOWN

3. **Ring Buffer Testing:**
   - Generate 10+ errors
   - Verify only last 5 stored
   - Check writeIndex wraps correctly

4. **Error Clearing:**
   - Trigger error (e.g., simulate jam)
   - Run successful dispense
   - Verify error marked as cleared in Serial output
   - Check `/health` shows `error.active = false`

**Integration Testing:**

1. **Real Hopper Errors:**
   - Physically jam hopper → expect ERROR_JAM_PERMANENT (3)
   - Disconnect coin sensor → expect ERROR_SENSOR_FAULT (6)
   - Verify errors appear in `/health` endpoint

2. **Self-Healing:**
   - Trigger jam error
   - Clear jam manually
   - Run successful dispense
   - Verify error clears from active status

3. **History Persistence:**
   - Generate multiple errors over time
   - Verify history grows up to 5 entries
   - Check oldest entries get overwritten

**TUI Testing:**

1. **Mock Server:**
   - Create test HTTP server returning error data
   - Test with 0 errors, 1 error, 5 errors
   - Verify dashboard displays correctly

2. **Live System:**
   - Connect to real ESP8266
   - Monitor during dispense operations
   - Verify errors appear/clear as expected
   - Check timestamps and age formatting

**Validation Checklist:**

- [ ] Error codes 1-7 decode correctly from pulse sequences
- [ ] ERROR_UNKNOWN (0) reported for malformed signals
- [ ] Ring buffer wraps correctly at 5 entries
- [ ] Active error clears on successful dispense
- [ ] Error history persists in ring buffer (cleared flag set)
- [ ] `/health` endpoint returns error + error_history fields
- [ ] TUI health panel shows decoded error type
- [ ] TUI error history panel displays last 5 errors with age
- [ ] Color coding works (yellow/red, muted for cleared)
- [ ] GPIO debug panel unchanged (backward compatibility)
- [ ] No memory leaks or crashes during extended operation
- [ ] Serial logging provides clear debug information

## Implementation Files

### Firmware (ESP8266)

**New files:**
- `firmware/dispenser/error_decoder.h` - ErrorDecoder class header
- `firmware/dispenser/error_decoder.cpp` - State machine implementation
- `firmware/dispenser/error_history.h` - ErrorHistory ring buffer class

**Modified files:**
- `firmware/dispenser/dispenser.ino` - Integrate ErrorDecoder in setup/loop
- `firmware/dispenser/hopper_control.h` - Add error history instance
- `firmware/dispenser/hopper_control.cpp` - Attach interrupt, integrate decoder
- `firmware/dispenser/http_server.cpp` - Add error fields to /health endpoint
- `firmware/dispenser/dispense_manager.cpp` - Clear errors on STATE_DONE

### TUI (Go)

**Modified files:**
- `dispenser-client-tui/client.go` - Add ErrorInfo and ErrorRecord structs
- `dispenser-client-tui/views.go` - Update health panel, add error history panel
- `dispenser-client-tui/styles.go` - May need additional styles for error severity

## Future Enhancements

**Not in this design (potential future work):**

1. **Flash persistence:** Store error history to ESP8266 filesystem (survives reboots)
2. **Error rate metrics:** Track errors per hour/day
3. **Error analytics:** Most common error types, time-of-day patterns
4. **Alerting:** Push notifications for critical errors
5. **Manual error reset:** Endpoint to clear active error without dispense
6. **Error code validation:** Verify pulse timing matches protocol spec exactly
7. **Diagnostics tab:** Dedicated TUI tab for error analysis and GPIO monitoring

## References

- **Azkoyen Protocol:** `docs/azkoyen-hopper-protocol.md` (section 3.5 - Error Signal)
- **Current Implementation:** `firmware/dispenser/hopper_control.cpp` (lines 128-134)
- **Dispense Protocol:** `dispenser-protocol.md` (health endpoint specification)

---

**Design Status:** ✅ **Approved - Ready for Implementation**
