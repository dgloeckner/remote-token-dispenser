# TUI Improvements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix schema mismatches, add WiFi RSSI display, replace burst test with simpler test cycle, and add GPIO debug overlay.

**Architecture:** Update HealthResponse to match actual firmware (WiFi/GPIO fields), reuse DispenseState for test cycle, add visual WiFi signal bars to dashboard, and implement toggleable GPIO debug overlay.

**Tech Stack:** Go, Bubble Tea TUI framework, Lip Gloss styling

---

## Task 1: Update Schema - WiFi and GPIO Structs

**Files:**
- Modify: `dispenser-client-tui/client.go:13-37`

**Step 1: Add WiFiInfo and GPIOInfo structs**

Add after the existing HealthResponse struct (around line 37):

```go
type WiFiInfo struct {
	RSSI int    `json:"rssi"`
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
```

**Step 2: Update HealthResponse struct**

Replace the HealthResponse struct (lines 13-21):

```go
type HealthResponse struct {
	Status    string        `json:"status"`
	Uptime    int           `json:"uptime"`
	Firmware  string        `json:"firmware"`
	WiFi      *WiFiInfo     `json:"wifi,omitempty"`
	Dispenser string        `json:"dispenser"`
	GPIO      *GPIOInfo     `json:"gpio,omitempty"`
	Metrics   Metrics       `json:"metrics"`
	ActiveTx  *ActiveTxInfo `json:"active_tx,omitempty"`
}
```

Note: Remove the old `HopperLow bool` field.

**Step 3: Update Metrics struct**

Add the `Failures` field to Metrics struct (around line 23):

```go
type Metrics struct {
	TotalDispenses int    `json:"total_dispenses"`
	Successful     int    `json:"successful"`
	Jams           int    `json:"jams"`
	Partial        int    `json:"partial"`
	Failures       int    `json:"failures"`
	LastError      string `json:"last_error"`
	LastErrorType  string `json:"last_error_type"`
}
```

**Step 4: Test with actual hardware**

Run the TUI and verify it parses the health response:

```bash
cd dispenser-client-tui
go run . --endpoint http://192.168.188.243 --api-key your-secret-api-key-here
```

Expected: TUI starts without JSON parse errors, dashboard loads.

**Step 5: Commit**

```bash
git add dispenser-client-tui/client.go
git commit -m "feat(tui): update health schema for WiFi and GPIO fields

- Add WiFiInfo and GPIOInfo structs
- Update HealthResponse to match actual firmware
- Add failures field to Metrics
- Remove incorrect top-level HopperLow field

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 2: Replace BurstState with TestState

**Files:**
- Modify: `dispenser-client-tui/model.go:49-57`
- Modify: `dispenser-client-tui/model.go:86-101`

**Step 1: Replace BurstState with TestState**

Replace the BurstState struct (lines 49-57) with:

```go
// TestState tracks a test cycle
type TestState struct {
	Preset      int       // 0=custom, 1=single, 2=typical, 3=stress
	CustomQty   int       // Custom quantity (1-20)
	Running     bool      // Test in progress
	LastResult  string    // Last test result message
	LastSuccess bool      // Last test succeeded
	LastTime    time.Duration // Last test duration
}
```

**Step 2: Update Model struct**

In the Model struct, replace:
- `burst BurstState` with `test TestState`
- Add `debugMode bool` field

```go
type Model struct {
	client *DispenserClient

	// Current view
	mode     viewMode
	width    int
	height   int
	quitting bool

	// Health data
	health        *HealthResponse
	healthErr     error
	lastHealthAt  time.Time
	connected     bool
	latencySamples []float64

	// Dispense state
	dispense     *DispenseState
	dispQuantity int

	// Test cycle (replaces burst)
	test TestState

	// Request log
	log       []LogEntry
	logScroll int

	// Debug mode
	debugMode bool

	// UI state
	ticker int
}
```

**Step 3: Update NewModel initialization**

Replace burst initialization (lines 98-100) with:

```go
test: TestState{
	Preset:    2, // Default to "typical purchase"
	CustomQty: 5,
},
```

**Step 4: Update view mode constants**

Replace `viewBurst` with `viewTest` (around line 18):

```go
const (
	viewDashboard viewMode = iota
	viewDispense
	viewTest  // Renamed from viewBurst
	viewLog
)
```

**Step 5: Commit**

```bash
git add dispenser-client-tui/model.go
git commit -m "feat(tui): replace burst test with test cycle state

- Replace BurstState with TestState (preset quantities)
- Add debugMode flag to Model
- Rename viewBurst to viewTest
- Initialize with 'typical purchase' preset

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 3: Add WiFi Signal Rendering Styles

**Files:**
- Modify: `dispenser-client-tui/styles.go`

**Step 1: Add WiFi signal strength styles**

Add at the end of the style declarations (after line 136):

```go
// WiFi signal strength styles
var (
	wifiExcellent = lipgloss.NewStyle().
			Foreground(colorSuccess).
			Bold(true)

	wifiGood = lipgloss.NewStyle().
			Foreground(colorWarning).
			Bold(true)

	wifiPoor = lipgloss.NewStyle().
			Foreground(colorError).
			Bold(true)
)

// GPIO debug panel style
var (
	debugPanelStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(colorSecondary).
			Padding(0, 1)
)
```

**Step 2: Commit**

```bash
git add dispenser-client-tui/styles.go
git commit -m "feat(tui): add WiFi and debug panel styles

- Add wifiExcellent/Good/Poor color styles
- Add debugPanelStyle for GPIO overlay

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 4: Add WiFi Signal Rendering Helper

**Files:**
- Modify: `dispenser-client-tui/views.go`

**Step 1: Add renderWiFiSignal helper function**

Add before the `renderStatusBadge` function (around line 525):

```go
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
	if strength >= 4 {
		style = wifiExcellent
	} else if strength >= 3 {
		style = wifiGood
	}

	return style.Render(bars) + " " + statusMuted.Render(fmt.Sprintf("%d dBm", rssi))
}
```

**Step 2: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): add WiFi signal strength renderer

- Add renderWiFiSignal helper with 5-bar display
- Color coded: green (excellent), yellow (good), red (poor)
- Shows visual bars + dBm value

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 5: Update Dashboard Health Panel with WiFi RSSI

**Files:**
- Modify: `dispenser-client-tui/views.go:135-180`

**Step 1: Update renderHealthPanel to show WiFi**

In the `renderHealthPanel` function, after the firmware line (around line 162), add WiFi display:

```go
// Firmware
lines = append(lines, labelStyle.Render("Firmware:")+" "+valueBold.Render(hl.Firmware))

// WiFi RSSI
if hl.WiFi != nil {
	wifiStr := renderWiFiSignal(hl.WiFi.RSSI)
	lines = append(lines, labelStyle.Render("WiFi:")+" "+wifiStr)
} else {
	lines = append(lines, labelStyle.Render("WiFi:")+" "+statusMuted.Render("─ unavailable"))
}
```

**Step 2: Update Hopper status to use GPIO**

Replace the hopper status section (around lines 165-169):

```go
// Hopper
hopperStr := statusOK.Render("● OK")
if hl.GPIO != nil && hl.GPIO.HopperLow.Active {
	hopperStr = statusWarning.Render("⚠ LOW")
}
lines = append(lines, labelStyle.Render("Hopper:")+" "+hopperStr)
```

**Step 3: Test with hardware**

Run: `go run . --endpoint http://192.168.188.243 --api-key your-key`

Expected: Dashboard shows WiFi signal bars with RSSI value

**Step 4: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): add WiFi RSSI to dashboard health panel

- Display WiFi signal bars with dBm value
- Fix hopper status to use GPIO.HopperLow.Active
- Show 'unavailable' fallback for missing WiFi data

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 6: Add GPIO Debug Overlay Renderer

**Files:**
- Modify: `dispenser-client-tui/views.go`

**Step 1: Add renderGPIODebugPanel helper**

Add after `renderLatencyPanel` function (around line 262):

```go
func (m Model) renderGPIODebugPanel(w int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("🔧 GPIO Debug")+" "+statusMuted.Render("[D] to hide"))
	lines = append(lines, "")

	if m.health == nil || m.health.GPIO == nil {
		lines = append(lines, statusMuted.Render("  GPIO data unavailable"))
		lines = append(lines, statusMuted.Render("  (Firmware may not support)"))
	} else {
		gpio := m.health.GPIO

		// Coin pulse
		coinStatus := statusMuted.Render("○ inactive")
		if gpio.CoinPulse.Active {
			coinStatus = statusError.Render("● ACTIVE")
		}
		lines = append(lines, fmt.Sprintf("  Coin Pulse:    raw=%d  %s",
			gpio.CoinPulse.Raw, coinStatus))

		// Error signal
		errStatus := statusMuted.Render("○ inactive")
		if gpio.ErrorSignal.Active {
			errStatus = statusError.Render("● ACTIVE")
		}
		lines = append(lines, fmt.Sprintf("  Error Signal:  raw=%d  %s",
			gpio.ErrorSignal.Raw, errStatus))

		// Hopper low
		hopperStatus := statusMuted.Render("○ inactive (not empty)")
		if gpio.HopperLow.Active {
			hopperStatus = statusWarning.Render("● ACTIVE (empty)")
		}
		lines = append(lines, fmt.Sprintf("  Hopper Low:    raw=%d  %s",
			gpio.HopperLow.Raw, hopperStatus))
	}

	content := strings.Join(lines, "\n")
	return debugPanelStyle.Width(w).Render(content)
}
```

**Step 2: Update renderDashboard to include GPIO overlay**

In `renderDashboard` function, after the latency panel (around line 127), add:

```go
// Latency sparkline
b.WriteString(m.renderLatencyPanel(w - 4))
b.WriteString("\n")

// GPIO debug overlay (if enabled)
if m.debugMode {
	b.WriteString(m.renderGPIODebugPanel(w - 4))
	b.WriteString("\n")
}

// Recent log
b.WriteString(m.renderRecentLog(w-4, 8))
```

**Step 3: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): add GPIO debug overlay panel

- Add renderGPIODebugPanel with raw GPIO states
- Show coin pulse, error signal, hopper low status
- Display in dashboard when debugMode enabled
- Graceful fallback for missing GPIO data

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 7: Add Debug Mode Toggle Keyboard Handler

**Files:**
- Modify: `dispenser-client-tui/model.go:303-340`

**Step 1: Add debug mode toggle to handleKey**

In the `handleKey` function, add debug toggle to global keys section (around line 326):

```go
case "r", "R":
	return m, m.fetchHealth()

case "d", "D":
	m.debugMode = !m.debugMode
	return m, nil
```

**Step 2: Test debug toggle**

Run: `go run . --endpoint http://192.168.188.243 --api-key your-key`

Actions:
1. Press `1` to go to Dashboard
2. Press `D` to toggle GPIO overlay
3. Verify overlay appears/disappears

Expected: GPIO debug panel toggles on/off

**Step 3: Commit**

```bash
git add dispenser-client-tui/model.go
git commit -m "feat(tui): add debug mode toggle with D key

- Toggle debugMode flag with D/d key
- Works globally on all tabs
- Shows/hides GPIO overlay on dashboard

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 8: Implement Test Tab View

**Files:**
- Modify: `dispenser-client-tui/views.go`

**Step 1: Add renderTestView function**

Replace the `renderBurstView` function (around lines 367-423) with:

```go
func (m Model) renderTestView(w, h int) string {
	var b strings.Builder

	if m.dispense != nil && m.dispense.State == "dispensing" {
		// Show running test with full progress (like dispense tab)
		var lines []string
		lines = append(lines, sectionHeader.Render("🧪 Test Running"))
		lines = append(lines, "")
		lines = append(lines, m.renderDispenseProgress()...)

		content := strings.Join(lines, "\n")
		panel := activePanelStyle.Width(w - 4).Render(content)
		b.WriteString(panel)
		b.WriteString("\n\n")
		b.WriteString(m.renderRecentLog(w-4, h-18))
		return b.String()
	}

	// Idle state - show test configuration
	var lines []string
	lines = append(lines, sectionHeader.Render("🧪 Test Cycle"))
	lines = append(lines, "")
	lines = append(lines, statusMuted.Render("  Quick Tests:"))
	lines = append(lines, "")

	// Preset options
	presets := []struct {
		key  string
		name string
		qty  int
	}{
		{"1", "Single token", 1},
		{"2", "Typical purchase", 3},
		{"3", "Stress test", 10},
		{"4", "Custom", m.test.CustomQty},
	}

	for i, preset := range presets {
		selected := (m.test.Preset == i+1)
		bullet := "  "
		if selected {
			bullet = "▶ "
		}

		line := fmt.Sprintf("%s[%s] %-17s (%d token", bullet, preset.key, preset.name, preset.qty)
		if preset.qty != 1 {
			line += "s"
		}
		line += ")"

		if selected {
			if i == 3 { // Custom
				line += "  " + statusMuted.Render("↑↓ to adjust")
			}
			lines = append(lines, valueBold.Render(line))
		} else {
			lines = append(lines, line)
		}
	}

	lines = append(lines, "")
	lines = append(lines, fmt.Sprintf("  Press %s to run selected test", keyStyle.Render("ENTER")))
	lines = append(lines, "")

	// Show last test result if available
	if m.test.LastResult != "" {
		lines = append(lines, statusMuted.Render("  ─────────────────────────────────────"))
		lines = append(lines, "  Last Test Result:")
		resultStyle := statusOK
		if !m.test.LastSuccess {
			resultStyle = statusError
		}
		lines = append(lines, "  "+resultStyle.Render(m.test.LastResult))
	}

	content := strings.Join(lines, "\n")
	panel := activePanelStyle.Width(w - 4).Render(content)
	b.WriteString(panel)
	b.WriteString("\n\n")
	b.WriteString(m.renderRecentLog(w-4, h-18))

	return b.String()
}
```

**Step 2: Update View function to call renderTestView**

In the `View` function, update the switch statement (around line 41):

```go
case viewTest:
	b.WriteString(m.renderTestView(w, h-5))
```

**Step 3: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): implement test tab view

- Add renderTestView with preset test options
- Show 4 presets: single/typical/stress/custom
- Reuse dispense progress display when running
- Display last test result

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 9: Add Test Tab Keyboard Handlers

**Files:**
- Modify: `dispenser-client-tui/model.go`

**Step 1: Replace handleBurstKeys with handleTestKeys**

Replace the `handleBurstKeys` function (around lines 361-390) with:

```go
func (m *Model) handleTestKeys(key string) (tea.Model, tea.Cmd) {
	if m.dispense != nil && m.dispense.State == "dispensing" {
		// Test running, no input allowed
		return m, nil
	}

	switch key {
	case "1":
		m.test.Preset = 1 // Single token
	case "2":
		m.test.Preset = 2 // Typical purchase
	case "3":
		m.test.Preset = 3 // Stress test
	case "4":
		m.test.Preset = 4 // Custom
	case "up", "k":
		if m.test.Preset == 4 && m.test.CustomQty < 20 {
			m.test.CustomQty++
		}
	case "down", "j":
		if m.test.Preset == 4 && m.test.CustomQty > 1 {
			m.test.CustomQty--
		}
	case "enter":
		// Start test with selected quantity
		qty := 0
		switch m.test.Preset {
		case 1:
			qty = 1
		case 2:
			qty = 3
		case 3:
			qty = 10
		case 4:
			qty = m.test.CustomQty
		}

		if qty > 0 {
			m.dispQuantity = qty // Set quantity for dispense
			m.dispense = nil     // Clear previous dispense state
			m.test.Running = true
			return m, m.startDispense()
		}
	case "c", "C":
		// Clear last result
		m.test.LastResult = ""
		m.test.LastSuccess = false
		m.test.LastTime = 0
	case "h", "H":
		// Force health refresh (useful after errors)
		return m, m.fetchHealth()
	}
	return m, nil
}
```

**Step 2: Update handleKey to route to handleTestKeys**

In `handleKey` function, update the mode-specific switch (around line 335):

```go
switch m.mode {
case viewDispense:
	return m.handleDispenseKeys(key)
case viewTest:
	return m.handleTestKeys(key)
case viewLog:
	return m.handleLogKeys(key)
}
```

**Step 3: Commit**

```bash
git add dispenser-client-tui/model.go
git commit -m "feat(tui): add test tab keyboard handlers

- Add handleTestKeys for preset selection (1-4)
- Support custom quantity adjustment (↑↓)
- Start test on Enter with selected quantity
- Clear result with C, refresh health with H

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 10: Update Test Result Recording

**Files:**
- Modify: `dispenser-client-tui/model.go:254-297`

**Step 1: Update dispensePollMsg handler to record test results**

In the `Update` function, modify the `dispensePollMsg` case (around lines 268-272) to record test results:

```go
case dispensePollMsg:
	if msg.result.Error != nil {
		m.addLog("GET", "/dispense/"+m.dispense.TxID, msg.result.StatusCode, msg.result.Latency, msg.result.Error.Error(), true)
		return m, m.pollDispense() // keep polling on transient errors
	}

	m.dispense.Dispensed = msg.resp.Dispensed
	m.dispense.State = msg.resp.State
	m.dispense.Error = msg.resp.Error
	m.addLatency(msg.result.Latency)
	m.addLog("GET", "/dispense/"+msg.resp.TxID, 200, msg.result.Latency,
		fmt.Sprintf("dispensed=%d/%d state=%s", msg.resp.Dispensed, msg.resp.Quantity, msg.resp.State), false)

	if msg.resp.State == "dispensing" {
		return m, m.pollDispense()
	}

	// Done or error - record result if test was running
	if m.test.Running {
		m.test.Running = false
		elapsed := time.Since(m.dispense.StartTime)
		m.test.LastTime = elapsed

		if msg.resp.State == "done" {
			m.test.LastSuccess = true
			m.test.LastResult = fmt.Sprintf("✓ Success - %d/%d tokens (%s)",
				msg.resp.Dispensed, msg.resp.Quantity, elapsed.Truncate(10*time.Millisecond))
		} else {
			m.test.LastSuccess = false
			errMsg := msg.resp.Error
			if errMsg == "" {
				errMsg = "unknown error"
			}
			m.test.LastResult = fmt.Sprintf("✗ Failed - %s (%d/%d tokens, %s)",
				errMsg, msg.resp.Dispensed, msg.resp.Quantity, elapsed.Truncate(10*time.Millisecond))
		}
	}

	// Refresh health and stop polling
	return m, m.fetchHealth()
```

**Step 2: Commit**

```bash
git add dispenser-client-tui/model.go
git commit -m "feat(tui): record test results after completion

- Capture test success/failure with timing
- Store last result message in TestState
- Display result on Test tab after completion

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 11: Update Tab Bar Labels

**Files:**
- Modify: `dispenser-client-tui/views.go:78-110`

**Step 1: Update renderTabBar tab names**

In the `renderTabBar` function, update the tabs array (around line 84):

```go
tabs := []struct {
	key  string
	name string
	mode viewMode
}{
	{"1", "Dashboard", viewDashboard},
	{"2", "Dispense", viewDispense},
	{"3", "Test", viewTest},  // Renamed from "Burst Test"
	{"4", "Log", viewLog},
}
```

**Step 2: Test tab navigation**

Run: `go run . --endpoint http://192.168.188.243 --api-key your-key`

Actions:
1. Press `1` - verify Dashboard shows
2. Press `2` - verify Dispense shows
3. Press `3` - verify Test shows (with presets)
4. Press `4` - verify Log shows

Expected: All tabs navigate correctly with updated labels

**Step 3: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): rename Burst Test tab to Test

- Update tab bar labels
- Test tab is now third tab (key 3)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 12: Update Footer Help Text

**Files:**
- Modify: `dispenser-client-tui/views.go:486-523`

**Step 1: Update renderFooter for Test tab**

In the `renderFooter` function, update the switch statement (around line 501):

```go
case viewTest:
	pairs = append([]struct{ key, desc string }{
		{"1-4", "select"},
		{"↑↓", "qty"},
		{"⏎", "run"},
		{"C", "clear"},
		{"H", "health"},
	}, pairs...)
```

**Step 2: Add debug mode help**

After the mode-specific help, add debug help if enabled (around line 513):

```go
var parts []string
for _, p := range pairs {
	parts = append(parts, keyStyle.Render(p.key)+" "+descStyle.Render(p.desc))
}

// Add debug mode indicator
if m.debugMode {
	parts = append(parts, statusMuted.Render("│"),
		statusSecondary.Render("DEBUG ON"))
}

help := strings.Join(parts, statusMuted.Render(" │ "))
```

Note: Add this style if missing:
```go
statusSecondary = lipgloss.NewStyle().
	Foreground(colorSecondary).
	Bold(true)
```

**Step 3: Commit**

```bash
git add dispenser-client-tui/views.go
git commit -m "feat(tui): update footer help for test tab

- Add test tab keyboard hints (select/qty/run/clear/health)
- Show DEBUG ON indicator when debug mode active

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 13: Add statusSecondary Style (if needed)

**Files:**
- Modify: `dispenser-client-tui/styles.go`

**Step 1: Check if statusSecondary exists**

Search for `statusSecondary` in styles.go.

If NOT found, add after `statusMuted` (around line 67):

```go
statusSecondary = lipgloss.NewStyle().
		Foreground(colorSecondary).
		Bold(true)
```

**Step 2: Commit (only if added)**

```bash
git add dispenser-client-tui/styles.go
git commit -m "feat(tui): add statusSecondary style

- Used for DEBUG ON indicator in footer

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 14: Clean Up Unused Burst Code

**Files:**
- Modify: `dispenser-client-tui/model.go`

**Step 1: Remove burstStepMsg and burstStep**

Remove these items if they still exist:
- `type burstStepMsg` message (around line 123)
- `func (m Model) burstStep()` command (around line 163)
- Case handler for `burstStepMsg` in Update (around line 274)

**Step 2: Verify build**

Run: `go build .`

Expected: No compilation errors

**Step 3: Commit**

```bash
git add dispenser-client-tui/model.go
git commit -m "refactor(tui): remove unused burst test code

- Remove burstStepMsg type
- Remove burstStep command
- Clean up old burst handlers

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 15: Integration Test with Hardware

**Files:**
- Test: All components with actual dispenser

**Step 1: Build the TUI**

```bash
cd dispenser-client-tui
go build -o token-tui .
```

**Step 2: Run with hardware**

```bash
./token-tui --endpoint http://192.168.188.243 --api-key your-secret-api-key-here
```

**Step 3: Test all features**

Dashboard:
- [ ] WiFi RSSI shows with signal bars
- [ ] Signal bars color-coded correctly
- [ ] Hopper status reflects GPIO.HopperLow.Active
- [ ] Press `D` to toggle GPIO debug overlay
- [ ] GPIO overlay shows all 3 signals with raw values

Test Tab:
- [ ] Press `3` to navigate to Test tab
- [ ] Press `1-4` to select presets
- [ ] Press `4` then `↑↓` to adjust custom quantity
- [ ] Press `Enter` to start test
- [ ] Verify progress bar and polling works
- [ ] After completion, verify last result shows
- [ ] Press `C` to clear result

Dispense Tab:
- [ ] Press `2` to navigate
- [ ] Verify normal dispense still works

Log Tab:
- [ ] Press `4` to navigate
- [ ] Verify all requests logged

**Step 4: Document any issues**

If bugs found, note them for fixes.

**Step 5: Final commit**

```bash
git add -A
git commit -m "test(tui): verify all features with hardware

- Tested WiFi RSSI display
- Tested GPIO debug overlay
- Tested Test tab with all presets
- Verified backward compatibility

All features working as expected.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 16: Update Documentation

**Files:**
- Modify: `dispenser-client-tui/README.md`

**Step 1: Update Features section**

Replace the Features section (around lines 44-68) with:

```markdown
## Features

### 1. Dashboard (Tab 1)
- Real-time health monitoring with auto-refresh every 5s
- ESP8266 status, uptime, firmware version, hopper status
- **WiFi signal strength with visual bars** (NEW)
- Dispense metrics: success rate, jams, partial dispenses, failures
- Latency sparkline with min/avg/max stats
- **GPIO debug overlay** - toggle with `D` key (NEW)
- Recent request log

### 2. Dispense (Tab 2)
- Interactive quantity selector (1-20 tokens)
- Visual coin indicator
- Live progress bar during dispensing with coin drop animation
- TX ID tracking, elapsed time, success/error feedback

### 3. Test Cycle (Tab 3) - UPDATED
- **Preset test quantities**: Single (1), Typical (3), Stress (10), Custom (1-20)
- Live progress bar during test with coin drop animation
- TX ID tracking, elapsed time, success/error feedback
- Last test result display with timing
- Quick health refresh with `H` key

### 4. Request Log (Tab 4)
- Full request history with timestamps, methods, status codes, latency
- Scrollable with keyboard navigation
- Color-coded status: green=2xx, yellow=4xx, red=5xx/errors
```

**Step 2: Update Keyboard Shortcuts table**

Update the keyboard shortcuts section (around lines 70-80):

```markdown
## Keyboard Shortcuts

| Key     | Action                           |
|---------|----------------------------------|
| `1-4`   | Switch tabs                      |
| `r`     | Force health refresh             |
| `d/D`   | Toggle GPIO debug overlay (NEW)  |
| `q`     | Quit                             |
| `↑/↓`   | Adjust quantity / scroll         |
| `Enter` | Start dispense / test            |
| `g/G`   | Jump to top/bottom of log        |
| `C`     | Clear result / log               |
| `H`     | Force health refresh (Test tab)  |
```

**Step 3: Commit**

```bash
git add dispenser-client-tui/README.md
git commit -m "docs(tui): update README with new features

- Document WiFi RSSI display
- Document GPIO debug overlay (D key)
- Update Test tab description (replaced Burst)
- Add new keyboard shortcuts

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Task 17: Update Protocol Documentation

**Files:**
- Modify: `dispenser-protocol.md`

**Step 1: Update health response example**

In the `GET /health` section (around line 119), update the response example to match actual firmware:

```json
{
  "status": "ok",
  "uptime": 84230,
  "firmware": "1.0.0",
  "wifi": {
    "rssi": -47,
    "ip": "192.168.188.243",
    "ssid": "Ponyhof"
  },
  "dispenser": "idle",
  "gpio": {
    "coin_pulse": {"raw": 1, "active": false},
    "error_signal": {"raw": 1, "active": false},
    "hopper_low": {"raw": 1, "active": false}
  },
  "metrics": {
    "total_dispenses": 1247,
    "successful": 1189,
    "jams": 3,
    "partial": 2,
    "failures": 55
  }
}
```

**Step 2: Update response fields table**

Update the response fields table (around line 136) to include new fields:

```markdown
| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Overall health: `"ok"`, `"degraded"`, `"error"` |
| `uptime` | integer | Seconds since boot |
| `firmware` | string | Firmware version |
| `wifi` | object | WiFi connection info (optional) |
| `wifi.rssi` | integer | Signal strength in dBm (-30 to -90) |
| `wifi.ip` | string | ESP8266 IP address |
| `wifi.ssid` | string | Connected WiFi network name |
| `dispenser` | string | Current state: `"idle"`, `"dispensing"`, `"error"` |
| `gpio` | object | GPIO pin states (optional) |
| `gpio.coin_pulse` | object | Coin sensor state |
| `gpio.error_signal` | object | Error signal state |
| `gpio.hopper_low` | object | Hopper low sensor state |
| `metrics.total_dispenses` | integer | Total dispense attempts since boot |
| `metrics.successful` | integer | Completed successfully |
| `metrics.jams` | integer | Jam errors detected |
| `metrics.partial` | integer | Partial dispenses (subset of jams) |
| `metrics.failures` | integer | Total failures (jams + other errors) |
```

**Step 3: Commit**

```bash
git add dispenser-protocol.md
git commit -m "docs: update protocol with WiFi and GPIO fields

- Add wifi object to health response
- Add gpio object with pin states
- Add failures field to metrics
- Update response example to match firmware

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Verification Checklist

After completing all tasks, verify:

**Schema & Parsing:**
- [ ] TUI parses health response without errors
- [ ] WiFi info displays correctly (or shows 'unavailable')
- [ ] GPIO info displays correctly (or shows 'unavailable')
- [ ] Hopper low warning triggers from GPIO

**WiFi Display:**
- [ ] Signal bars render correctly (▂▃▅▆█)
- [ ] Color coding works (green/yellow/red)
- [ ] dBm value shows accurate RSSI

**GPIO Debug:**
- [ ] `D` key toggles overlay on/off
- [ ] Overlay shows all 3 GPIO signals
- [ ] Active states highlight correctly
- [ ] Works across all tabs

**Test Tab:**
- [ ] Preset selection works (1-4 keys)
- [ ] Custom quantity adjusts (↑↓)
- [ ] Test runs to completion
- [ ] Progress bar updates in real-time
- [ ] Last result displays correctly
- [ ] Clear result works (C key)
- [ ] Health refresh works (H key)

**No Regressions:**
- [ ] Dashboard still shows metrics
- [ ] Dispense tab still works
- [ ] Log tab still works
- [ ] All keyboard shortcuts work

---

## Success Criteria

1. ✅ TUI parses actual hardware response without errors
2. ✅ WiFi RSSI visible on dashboard with visual indicators
3. ✅ GPIO overlay provides diagnostic info when needed
4. ✅ Test tab simpler and faster than old burst test
5. ✅ All existing functionality preserved
6. ✅ Documentation updated and accurate

---

**Implementation complete!** The TUI now has full schema alignment, WiFi diagnostics, GPIO debugging, and a streamlined test interface.
