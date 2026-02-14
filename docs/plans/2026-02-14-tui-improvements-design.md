# TUI Improvements Design

**Date:** 2026-02-14
**Status:** Approved
**Author:** Claude (with user collaboration)

## Overview

Improve the dispenser-client-tui to fix schema mismatches with actual hardware, replace the burst test with a simpler test cycle, and add diagnostic features (WiFi RSSI, GPIO debug overlay).

## Goals

1. Fix `HealthResponse` schema to match actual ESP8266 firmware response
2. Replace Burst Test tab with simpler Test Cycle tab (preset quantities)
3. Add WiFi RSSI display with visual signal bars to Dashboard
4. Add hidden GPIO debug overlay (toggle with `D` key)

## Architecture

### File Changes

```
dispenser-client-tui/
├── client.go        ← Update HealthResponse struct (WiFi, GPIO, failures)
├── model.go         ← Replace BurstState with TestState, add debugMode flag
├── views.go         ← Update dashboard WiFi display, new Test view, GPIO overlay
└── styles.go        ← Add WiFi signal bar styles, GPIO debug panel styles
```

### Tab Structure

Keep 4 tabs, replace Burst Test:
1. **Dashboard** - Health, metrics, WiFi RSSI, latency (+ GPIO debug overlay when enabled)
2. **Dispense** - Manual token dispense (unchanged)
3. **Test** - Test cycle with preset quantities (NEW - replaces Burst)
4. **Log** - Request history (unchanged)

## Design Details

### 1. Schema Fixes

#### Updated HealthResponse

```go
type HealthResponse struct {
    Status    string       `json:"status"`
    Uptime    int          `json:"uptime"`
    Firmware  string       `json:"firmware"`
    WiFi      *WiFiInfo    `json:"wifi,omitempty"`      // NEW
    Dispenser string       `json:"dispenser"`
    GPIO      *GPIOInfo    `json:"gpio,omitempty"`      // NEW
    Metrics   Metrics      `json:"metrics"`
    ActiveTx  *ActiveTxInfo `json:"active_tx,omitempty"`
}

type WiFiInfo struct {
    RSSI int    `json:"rssi"`  // Signal strength in dBm
    IP   string `json:"ip"`
    SSID string `json:"ssid"`
}

type GPIOInfo struct {
    CoinPulse struct {
        Raw    int  `json:"raw"`
        Active bool `json:"active"`
    } `json:"coin_pulse"`
    ErrorSignal struct {
        Raw    int  `json:"raw"`
        Active bool `json:"active"`
    } `json:"error_signal"`
    HopperLow struct {
        Raw    int  `json:"raw"`
        Active bool `json:"active"`
    } `json:"hopper_low"`
}

type Metrics struct {
    TotalDispenses int    `json:"total_dispenses"`
    Successful     int    `json:"successful"`
    Jams           int    `json:"jams"`
    Partial        int    `json:"partial"`
    Failures       int    `json:"failures"`       // NEW
    LastError      string `json:"last_error"`
    LastErrorType  string `json:"last_error_type"`
}
```

#### Backward Compatibility

- All new fields use `omitempty` and pointers
- Gracefully handles older firmware without WiFi/GPIO fields
- Hopper status: `health.GPIO != nil && health.GPIO.HopperLow.Active`

### 2. Test Tab Implementation

#### Test State

```go
// model.go - Replace BurstState with TestState
type TestState struct {
    Preset   int  // 0=custom, 1=single, 2=typical, 3=stress
    Custom   int  // Custom quantity (1-20)
    Running  bool // Test in progress
}

// Reuse existing DispenseState for active test tracking
```

#### UI Layout - Idle State

```
╭─────────────────────────────────────────────╮
│ 🧪 Test Cycle                               │
│                                             │
│ Quick Tests:                                │
│   [1] Single token    (1 token)            │
│   [2] Typical purchase (3 tokens)          │
│   [3] Stress test     (10 tokens)          │
│   [4] Custom: 5 ↑↓    (1-20 tokens)        │
│                                             │
│ Press ENTER to run selected test           │
│                                             │
│ ─────────────────────────────────────────  │
│ Last Test Result:                          │
│ ✓ Success - 3/3 tokens (7.2s)              │
╰─────────────────────────────────────────────╯
```

#### UI Layout - Running State

Full transaction details (consistent with Dispense tab):

```
╭─────────────────────────────────────────────╮
│ 🧪 Test Running                             │
│                                             │
│ TX: a3f8c012                                │
│ Quantity: 3 tokens                          │
│                                             │
│ [██████████░░░░░] 2/3                       │
│                                             │
│ ⟳ DISPENSING 🪙↓                            │
│ elapsed: 5.8s                               │
╰─────────────────────────────────────────────╯

Recent Requests: [log entries below...]
```

#### UI Layout - Error State

```
╭─────────────────────────────────────────────╮
│ 🧪 Test Failed                              │
│                                             │
│ TX: a3f8c012                                │
│ Status: error                               │
│ Dispensed: 2/5 (partial)                    │
│ Elapsed: 8.4s                               │
│                                             │
│ Error Details:                              │
│ • Type: jam                                 │
│ • Time: 15:42:03                            │
│ • GPIO: error_signal active                 │
│                                             │
│ [ENTER] New test  [H] Check health          │
╰─────────────────────────────────────────────╯
```

#### Keyboard Controls

- `1-4`: Select preset/custom
- `↑↓`: Adjust custom quantity (when preset 4 selected)
- `Enter`: Run test
- `C`: Clear last result
- `H`: Force health refresh (when error shown)

#### Test Quantities

- Preset 1: 1 token (quick check)
- Preset 2: 3 tokens (typical purchase)
- Preset 3: 10 tokens (stress test)
- Preset 4: Custom 1-20 (user adjustable)

### 3. Dashboard WiFi RSSI Display

#### Visual Signal Bars

```go
// styles.go - New WiFi signal styles
var (
    wifiExcellent = lipgloss.NewStyle().Foreground(colorSuccess).Bold(true)  // -30 to -50 dBm
    wifiGood      = lipgloss.NewStyle().Foreground(colorWarning).Bold(true)  // -51 to -70 dBm
    wifiPoor      = lipgloss.NewStyle().Foreground(colorError).Bold(true)    // -71+ dBm
)

func renderWiFiSignal(rssi int) string {
    // 5-bar display: ▂▃▅▆█
    bars := ""
    strength := 0

    if rssi >= -50 {
        bars = "▂▃▅▆█"
        strength = 5
    } else if rssi >= -60 {
        bars = "▂▃▅▆_"
        strength = 4
    } else if rssi >= -70 {
        bars = "▂▃▅__"
        strength = 3
    } else if rssi >= -80 {
        bars = "▂▃___"
        strength = 2
    } else {
        bars = "▂____"
        strength = 1
    }

    // Color code based on strength
    style := wifiPoor
    if strength >= 4 { style = wifiExcellent }
    else if strength >= 3 { style = wifiGood }

    return style.Render(bars) + " " + statusMuted.Render(fmt.Sprintf("%d dBm", rssi))
}
```

#### Updated Health Panel

```
╭─ ⚡ Health ─────────────╮
│ Status:     ● OK        │
│ Dispenser:  idle        │
│ Uptime:     23h 27m     │
│ Firmware:   1.2.0       │
│ WiFi:  ▂▃▅▆█ -47 dBm    │  ← NEW: Visual + numeric
│ Hopper:     ● OK        │
╰─────────────────────────╯
```

#### Fallback

If `health.WiFi == nil`:
```
WiFi:  ─ unavailable
```

### 4. GPIO Debug Overlay

#### Debug Mode State

```go
// model.go - Add to Model struct
type Model struct {
    // ... existing fields ...
    debugMode bool  // Toggle GPIO overlay with 'D' key
}
```

#### Overlay Display

When `debugMode = true`, show GPIO panel below metrics on Dashboard:

```
╭─ ⚡ Health ─────────╮  ╭─ 📊 Metrics ────────╮
│ Status:     ● OK    │  │ Total:      1247    │
│ Dispenser:  idle    │  │ Success:    95.4%   │
│ WiFi:  ▂▃▅▆█ -47dBm │  │ Jams:       3       │
│ Hopper:     ● OK    │  │ Partial:    2       │
╰─────────────────────╯  ╰─────────────────────╯

╭─ 🔧 GPIO Debug ──────────────────────────────╮
│ Coin Pulse:    raw=1  ○ inactive             │
│ Error Signal:  raw=1  ○ inactive             │
│ Hopper Low:    raw=1  ○ inactive (not empty) │
│                                               │
│ Press [D] to hide                             │
╰───────────────────────────────────────────────╯
```

#### Active State Indicators

```go
func renderGPIOState(active bool) string {
    if active {
        return statusError.Render("● ACTIVE")
    }
    return statusMuted.Render("○ inactive")
}
```

#### Keyboard Control

- `D` or `d`: Toggle debug overlay (global, works on all tabs)
- Overlay persists across tab switches when enabled

#### Fallback

If `health.GPIO == nil`:
```
╭─ 🔧 GPIO Debug ───────────────╮
│ GPIO data unavailable         │
│ (Firmware may not support)    │
╰───────────────────────────────╯
```

## Implementation Notes

### Code Reuse

- Test tab reuses `DispenseState` and polling logic from Dispense tab
- Ensures consistent behavior and reduces duplication
- Same progress bar, coin animation, and status display

### State Management

New state additions:
```go
// model.go
type Model struct {
    // ... existing fields ...

    // Replace burst with test
    test TestState  // Replaces: burst BurstState

    // New debug mode
    debugMode bool
}
```

### View Modes

```go
const (
    viewDashboard viewMode = iota
    viewDispense
    viewTest     // Replaces: viewBurst
    viewLog
)
```

## Testing Checklist

- [ ] Health response parses WiFi/GPIO correctly
- [ ] WiFi RSSI displays with correct color coding
- [ ] Hopper low warning triggers from `gpio.hopper_low.active`
- [ ] Test tab preset selection works (1-4 keys)
- [ ] Custom quantity adjustment works (↑↓ keys)
- [ ] Test cycle starts and polls correctly
- [ ] Test error state shows detailed diagnostics
- [ ] GPIO debug overlay toggles with D key
- [ ] Debug overlay shows all GPIO states correctly
- [ ] Backward compatibility with older firmware (missing WiFi/GPIO)

## Success Criteria

1. **Schema alignment**: TUI parses actual hardware response without errors
2. **WiFi visibility**: Signal strength visible at a glance on dashboard
3. **Simplified testing**: Preset test values make testing faster than old burst test
4. **Hardware diagnostics**: GPIO overlay helps troubleshoot hopper/sensor issues
5. **No regressions**: Existing dispense/log functionality unchanged

## Future Enhancements (Out of Scope)

- Configurable test presets (via config file)
- Test history/statistics tracking
- WiFi reconnection alerts
- GPIO state change logging
