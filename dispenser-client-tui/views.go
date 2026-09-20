package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/charmbracelet/lipgloss"
)

// View renders the full TUI
func (m Model) View() string {
	if m.quitting {
		return ""
	}

	w := m.width
	if w < 40 {
		w = 80
	}
	h := m.height
	if h < 10 {
		h = 40
	}

	var b strings.Builder

	// Title bar
	b.WriteString(m.renderTitleBar(w))
	b.WriteString("\n")

	// Tab bar
	b.WriteString(m.renderTabBar(w))
	b.WriteString("\n")

	// Main content
	switch m.mode {
	case viewDashboard:
		b.WriteString(m.renderDashboard(w, h-5))
	case viewDispense:
		b.WriteString(m.renderDispenseView(w, h-5))
	case viewLog:
		b.WriteString(m.renderLogView(w, h-5))
	case viewTest:
		b.WriteString(m.renderTestView(w, h-5))
	}

	// Footer help
	b.WriteString(m.renderFooter(w))

	return b.String()
}

// --- Title Bar ---

func (m Model) renderTitleBar(w int) string {
	title := titleStyle.Render(" 🪙 Token Dispenser TUI ")

	connStatus := ""
	if m.connected {
		connStatus = statusOK.Render("● connected")
	} else {
		connStatus = statusError.Render("● disconnected")
	}

	endpoint := statusMuted.Render(m.client.BaseURL)

	rightSide := fmt.Sprintf("%s  %s", endpoint, connStatus)
	gap := w - lipgloss.Width(title) - lipgloss.Width(rightSide) - 1
	if gap < 1 {
		gap = 1
	}

	return title + strings.Repeat(" ", gap) + rightSide
}

// --- Tab Bar ---

func (m Model) renderTabBar(w int) string {
	tabs := []struct {
		key  string
		name string
		mode viewMode
	}{
		{"1", "Dashboard", viewDashboard},
		{"2", "Dispense", viewDispense},
		{"3", "Test", viewTest},
		{"4", "Log", viewLog},
	}

	var parts []string
	for _, t := range tabs {
		label := fmt.Sprintf(" %s:%s ", t.key, t.name)
		if m.mode == t.mode {
			parts = append(parts, lipgloss.NewStyle().
				Background(colorPrimary).
				Foreground(colorText).
				Bold(true).
				Render(label))
		} else {
			parts = append(parts, lipgloss.NewStyle().
				Foreground(colorDim).
				Render(label))
		}
	}

	bar := strings.Join(parts, statusMuted.Render("│"))
	sep := statusMuted.Render(strings.Repeat("─", w))
	return bar + "\n" + sep
}

// --- Dashboard View ---

func (m Model) renderDashboard(w, h int) string {
	var b strings.Builder

	// Top half: health info
	leftCol := m.renderHealthPanel(w/2 - 2)
	rightCol := m.renderMetricsPanel(w/2 - 2)

	topRow := lipgloss.JoinHorizontal(lipgloss.Top, leftCol, "  ", rightCol)
	b.WriteString(topRow)
	b.WriteString("\n")

	// Latency sparkline
	b.WriteString(m.renderLatencyPanel(w - 4))
	b.WriteString("\n")

	// Error history panel
	b.WriteString(m.renderErrorHistoryPanel(w - 4))
	b.WriteString("\n")

	// GPIO debug overlay (if enabled)
	if m.debugMode {
		b.WriteString(m.renderGPIODebugPanel(w - 4))
		b.WriteString("\n")
	}

	// Recent log (last 5 entries)
	b.WriteString(m.renderRecentLog(w-4, 8))

	return b.String()
}

func (m Model) renderHealthPanel(w int) string {
	var lines []string

	lines = append(lines, sectionHeader.Render("⚡ Health"))
	lines = append(lines, "")

	if m.health == nil {
		if m.healthErr != nil {
			lines = append(lines, labelStyle.Render("Status:")+" "+statusError.Render("ERROR"))
			lines = append(lines, labelStyle.Render("Error:")+" "+errorStyle.Render(truncate(m.healthErr.Error(), w-20)))
		} else {
			lines = append(lines, labelStyle.Render("Status:")+" "+statusMuted.Render("connecting..."))
		}
	} else {
		hl := m.health
		// One state and one fault (issue #6).  The pair used to be `status`
		// and `dispenser`, ORed together here — with nothing deciding which
		// of the two won when they disagreed.
		lines = append(lines, labelStyle.Render("State:")+" "+renderDeviceState(hl.State))
		lines = append(lines, labelStyle.Render("Fault:")+" "+renderFault(hl.Fault, hl.FaultCode))

		// Uptime
		lines = append(lines, labelStyle.Render("Uptime:")+" "+valueBold.Render(formatDuration(hl.Uptime)))

		// Firmware
		lines = append(lines, labelStyle.Render("Firmware:")+" "+valueBold.Render(hl.Firmware))

		// WiFi RSSI
		if hl.WiFi != nil {
			wifiStr := renderWiFiSignal(hl.WiFi.RSSI)
			lines = append(lines, labelStyle.Render("WiFi:")+" "+wifiStr)
		} else {
			lines = append(lines, labelStyle.Render("WiFi:")+" "+statusMuted.Render("─ unavailable"))
		}

		// A faulted device needs a human and says so.  There is no reset
		// button here on purpose: the way out is a power cycle (owner
		// decision, 2026-09-20).
		if hl.Fault != "" && hl.Fault != "none" {
			lines = append(lines, labelStyle.Render("Action:")+" "+
				statusWarning.Render("clear the jam, refill if empty, pull the plug for 5 s"))
		}

		// Active TX
		if hl.ActiveTx != nil {
			lines = append(lines, labelStyle.Render("Active TX:")+" "+
				coinStyle.Render(fmt.Sprintf("%s (%d/%d)", hl.ActiveTx.TxID, hl.ActiveTx.Dispensed, hl.ActiveTx.Quantity)))
		}
	}

	content := strings.Join(lines, "\n")
	return panelStyle.Width(w).Render(content)
}

func (m Model) renderMetricsPanel(w int) string {
	var lines []string

	lines = append(lines, sectionHeader.Render("📊 Metrics"))
	lines = append(lines, "")

	if m.health == nil {
		lines = append(lines, statusMuted.Render("  waiting for data..."))
	} else {
		met := m.health.Metrics

		// Total
		lines = append(lines, labelStyle.Render("Total Dispenses:")+" "+valueBold.Render(fmt.Sprintf("%d", met.TotalDispenses)))

		// Success rate
		rate := float64(0)
		if met.TotalDispenses > 0 {
			rate = float64(met.Successful) / float64(met.TotalDispenses) * 100
		}
		rateStyle := statusOK
		if rate < 90 {
			rateStyle = statusWarning
		}
		if rate < 80 {
			rateStyle = statusError
		}
		lines = append(lines, labelStyle.Render("Success Rate:")+" "+rateStyle.Render(fmt.Sprintf("%.1f%%", rate))+
			statusMuted.Render(fmt.Sprintf(" (%d/%d)", met.Successful, met.TotalDispenses)))

		// Jams
		jamStyle := statusOK
		if met.Jams > 0 {
			jamStyle = statusWarning
		}
		lines = append(lines, labelStyle.Render("Jams:")+" "+jamStyle.Render(fmt.Sprintf("%d", met.Jams)))

		// Partial
		lines = append(lines, labelStyle.Render("Partial:")+" "+valueBold.Render(fmt.Sprintf("%d", met.Partial)))

		// Failures
		failStyle := statusOK
		if met.Failures > 0 {
			failStyle = statusError
		}
		lines = append(lines, labelStyle.Render("Failures:")+" "+failStyle.Render(fmt.Sprintf("%d", met.Failures)))

		// Last error
		if met.LastError != "" {
			lines = append(lines, labelStyle.Render("Last Error:")+" "+statusError.Render(met.LastErrorType)+
				" "+statusMuted.Render(met.LastError))
		}
	}

	content := strings.Join(lines, "\n")
	return panelStyle.Width(w).Render(content)
}

func (m Model) renderLatencyPanel(w int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("📈 Latency")+" "+statusMuted.Render("(ms)"))

	if len(m.latencySamples) < 2 {
		lines = append(lines, statusMuted.Render("  collecting samples..."))
	} else {
		spark := renderSparkline(m.latencySamples, w-4)
		lines = append(lines, spark)

		// Stats
		minL, maxL, avgL := latencyStats(m.latencySamples)
		stats := fmt.Sprintf("  min:%s  avg:%s  max:%s  samples:%s",
			statusOK.Render(fmt.Sprintf("%.0fms", minL)),
			valueBold.Render(fmt.Sprintf("%.0fms", avgL)),
			statusWarning.Render(fmt.Sprintf("%.0fms", maxL)),
			statusMuted.Render(fmt.Sprintf("%d", len(m.latencySamples))),
		)
		lines = append(lines, stats)
	}

	content := strings.Join(lines, "\n")
	return panelStyle.Width(w).Render(content)
}

func (m Model) renderGPIODebugPanel(w int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("🔧 GPIO Debug")+" "+statusMuted.Render("[D] to hide"))
	lines = append(lines, "")

	if m.debug == nil || m.debug.GPIO == nil {
		lines = append(lines, statusMuted.Render("  GPIO data unavailable"))
		lines = append(lines, statusMuted.Render("  (GET /debug, key required)"))
	} else {
		gpio := m.debug.GPIO

		// Coin pulse (active=true is idle/default for active-LOW signal)
		coinStatus := statusError.Render("● COIN DETECTED")
		if gpio.CoinPulse.Active {
			coinStatus = statusMuted.Render("○ idle (default)")
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

		// There is no empty-sensor line: the hopper does not have the sensor
		// fitted, so D6 read "not empty" forever (issue #6).
	}

	content := strings.Join(lines, "\n")
	return debugPanelStyle.Width(w).Render(content)
}

func (m Model) renderErrorHistoryPanel(w int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("🚨 Recent Errors (Last 5)"))
	lines = append(lines, "")

	if m.health == nil || m.health.ErrorHistory == nil || len(m.health.ErrorHistory) == 0 {
		lines = append(lines, statusOK.Render("  ✓ No errors recorded"))
	} else {
		for _, err := range m.health.ErrorHistory {
			// Every recorded error stands until a power cycle; there is
			// nothing to mark as cleared any more (issue #6).
			status := "⚠"
			style := statusWarning // Sensor errors = yellow
			if err.Code >= 3 {
				style = statusError // Critical errors = red
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

// --- Dispense View ---

func (m Model) renderDispenseView(w, h int) string {
	var b strings.Builder

	// Quantity selector
	var lines []string
	lines = append(lines, sectionHeader.Render("🪙 Dispense Tokens"))
	lines = append(lines, "")

	// Coin visualization
	coinRow := "  "
	for i := 0; i < m.dispQuantity; i++ {
		coinRow += coinStyle.Render("⬤ ")
	}
	for i := m.dispQuantity; i < 20; i++ {
		coinRow += statusMuted.Render("○ ")
	}
	lines = append(lines, coinRow)
	lines = append(lines, "")

	lines = append(lines, fmt.Sprintf("  Quantity: %s  %s",
		valueBold.Render(fmt.Sprintf("%d", m.dispQuantity)),
		statusMuted.Render("(↑/↓ to adjust)")))
	lines = append(lines, "")

	if m.dispense != nil {
		lines = append(lines, m.renderDispenseProgress()...)
	} else {
		lines = append(lines, fmt.Sprintf("  Press %s to dispense", keyStyle.Render("ENTER")))
	}

	content := strings.Join(lines, "\n")
	panel := activePanelStyle.Width(w - 4).Render(content)
	b.WriteString(panel)
	b.WriteString("\n\n")

	// Show recent log below
	b.WriteString(m.renderRecentLog(w-4, h-15))

	return b.String()
}

func (m Model) renderDispenseProgress() []string {
	d := m.dispense
	var lines []string

	lines = append(lines, fmt.Sprintf("  TX: %s", valueBold.Render(d.TxID)))

	// Progress bar
	filled := d.Dispensed
	total := d.Quantity
	if total == 0 {
		total = 1
	}

	barWidth := 30
	filledWidth := (filled * barWidth) / total
	emptyWidth := barWidth - filledWidth

	bar := "  ["
	bar += progressFilled.Render(strings.Repeat("█", filledWidth))
	bar += progressEmpty.Render(strings.Repeat("░", emptyWidth))
	bar += fmt.Sprintf("] %d/%d", filled, d.Quantity)
	lines = append(lines, bar)

	// Coin drop animation
	elapsed := time.Since(d.StartTime)

	switch d.State {
	case "dispensing":
		frames := []string{"🪙  ↓", " 🪙 ↓", "  🪙↓", "   🪙"}
		frame := frames[m.ticker%len(frames)]
		lines = append(lines, "")
		lines = append(lines, dispensingStyle.Render("  DISPENSING "+frame))
		lines = append(lines, statusMuted.Render(fmt.Sprintf("  elapsed: %s", elapsed.Truncate(time.Millisecond))))

	case "done":
		lines = append(lines, "")
		lines = append(lines, statusOK.Render(fmt.Sprintf("  ✓ COMPLETE — %d tokens dispensed", d.Dispensed)))
		lines = append(lines, statusMuted.Render(fmt.Sprintf("  total time: %s", elapsed.Truncate(time.Millisecond))))
		lines = append(lines, "")
		lines = append(lines, fmt.Sprintf("  Press %s to dispense again", keyStyle.Render("ENTER")))

	case "error":
		lines = append(lines, "")
		errMsg := d.Error
		if errMsg == "" {
			errMsg = "unknown error"
		}
		lines = append(lines, statusError.Render(fmt.Sprintf("  ✗ ERROR: %s", errMsg)))
		if d.Dispensed > 0 {
			lines = append(lines, statusWarning.Render(fmt.Sprintf("  ⚠ Partial dispense: %d/%d tokens", d.Dispensed, d.Quantity)))
		}
		if d.CountUnreliable {
			lines = append(lines, statusWarning.Render(
				"  ⚠ Count is a LOWER BOUND — the device lost power mid-dispense"))
			lines = append(lines, statusMuted.Render("    count the tray before reconciling"))
		}
		lines = append(lines, "")
		lines = append(lines, fmt.Sprintf("  Press %s to retry", keyStyle.Render("ENTER")))
	}

	return lines
}

// --- Test View ---

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

// --- Log View ---

func (m Model) renderLogView(w, h int) string {
	var lines []string

	lines = append(lines, sectionHeader.Render(fmt.Sprintf("📋 Request Log (%d entries)", len(m.log)))+
		"  "+statusMuted.Render("[C]lear  [g]top  [G]bottom"))
	lines = append(lines, "")

	if len(m.log) == 0 {
		lines = append(lines, statusMuted.Render("  No requests yet..."))
	} else {
		visibleLines := h - 6
		if visibleLines < 5 {
			visibleLines = 15
		}

		start := m.logScroll
		end := start + visibleLines
		if end > len(m.log) {
			end = len(m.log)
			start = max(0, end-visibleLines)
		}

		for _, entry := range m.log[start:end] {
			lines = append(lines, formatLogEntry(entry, w-6))
		}

		// Scroll indicator
		if len(m.log) > visibleLines {
			pos := "top"
			if start > 0 && end < len(m.log) {
				pos = fmt.Sprintf("%d/%d", start+1, len(m.log))
			} else if end >= len(m.log) {
				pos = "end"
			}
			lines = append(lines, statusMuted.Render(fmt.Sprintf("  ── %s ──", pos)))
		}
	}

	content := strings.Join(lines, "\n")
	return activePanelStyle.Width(w - 4).Render(content)
}

func (m Model) renderRecentLog(w, maxLines int) string {
	var lines []string
	lines = append(lines, sectionHeader.Render("📋 Recent Requests"))

	if len(m.log) == 0 {
		lines = append(lines, statusMuted.Render("  waiting for requests..."))
	} else {
		start := max(0, len(m.log)-maxLines)
		for _, entry := range m.log[start:] {
			lines = append(lines, formatLogEntry(entry, w-4))
		}
	}

	content := strings.Join(lines, "\n")
	return panelStyle.Width(w).Render(content)
}

// --- Footer ---

func (m Model) renderFooter(w int) string {
	pairs := []struct{ key, desc string }{
		{"1-4", "tabs"},
		{"r", "refresh"},
		{"q", "quit"},
	}

	switch m.mode {
	case viewDispense:
		pairs = append([]struct{ key, desc string }{
			{"↑↓", "qty"},
			{"⏎", "dispense"},
		}, pairs...)
	case viewTest:
		pairs = append([]struct{ key, desc string }{
			{"1-4", "select"},
			{"↑↓", "qty"},
			{"⏎", "run"},
			{"C", "clear"},
			{"H", "health"},
		}, pairs...)
	case viewLog:
		pairs = append([]struct{ key, desc string }{
			{"↑↓", "scroll"},
			{"g/G", "top/bottom"},
			{"C", "clear"},
		}, pairs...)
	}

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
	sep := statusMuted.Render(strings.Repeat("─", w))
	return sep + "\n" + help
}

// --- Rendering Helpers ---

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

// renderDeviceState renders GET /health's single `state` (issue #6).
func renderDeviceState(state string) string {
	switch state {
	case "idle":
		return statusOK.Render("● idle")
	case "dispensing":
		return dispensingStyle.Render("⟳ dispensing")
	case "fault":
		return statusError.Render("✗ fault")
	case "":
		return statusMuted.Render("? missing")
	default:
		return statusMuted.Render("? " + state)
	}
}

// renderFault renders the device-level fault and, for a hopper error, its
// Azkoyen code.  A missing field is shown as missing: a device that does not
// send `fault` is not a device without a fault.
func renderFault(fault string, code *int) string {
	switch fault {
	case "none":
		return statusOK.Render("none")
	case "jam":
		return statusError.Render("⚠ jam")
	case "hopper_error":
		if code != nil && *code > 0 {
			return statusError.Render(fmt.Sprintf("⚠ hopper_error (code %d)", *code))
		}
		return statusError.Render("⚠ hopper_error")
	case "":
		return statusMuted.Render("? missing")
	default:
		return statusMuted.Render("? " + fault)
	}
}

func renderSparkline(data []float64, width int) string {
	blocks := []string{"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"}

	// Find min/max
	minVal, maxVal := data[0], data[0]
	for _, v := range data {
		if v < minVal {
			minVal = v
		}
		if v > maxVal {
			maxVal = v
		}
	}

	// Scale data to fit width
	step := max(1, len(data)/width)
	var sampled []float64
	for i := 0; i < len(data); i += step {
		end := min(i+step, len(data))
		sum := 0.0
		for _, v := range data[i:end] {
			sum += v
		}
		sampled = append(sampled, sum/float64(end-i))
	}

	// Render
	spread := maxVal - minVal
	if spread == 0 {
		spread = 1
	}

	var sb strings.Builder
	sb.WriteString("  ")
	for _, v := range sampled {
		idx := int((v - minVal) / spread * float64(len(blocks)-1))
		if idx >= len(blocks) {
			idx = len(blocks) - 1
		}
		if idx < 0 {
			idx = 0
		}

		// Color high values differently
		if v > maxVal*0.8 {
			sb.WriteString(sparkHighStyle.Render(blocks[idx]))
		} else {
			sb.WriteString(sparkStyle.Render(blocks[idx]))
		}
	}

	return sb.String()
}

func formatLogEntry(entry LogEntry, w int) string {
	ts := logTimestamp.Render(entry.Time.Format("15:04:05"))
	method := logMethod.Render(entry.Method)

	path := entry.Path
	if len(path) > 25 {
		path = path[:22] + "..."
	}
	pathStr := logPath.Render(fmt.Sprintf("%-25s", path))

	var statusStr string
	switch {
	case entry.StatusCode == 0:
		statusStr = statusError.Render("ERR")
	case entry.StatusCode < 300:
		statusStr = logStatus200.Render(fmt.Sprintf("%d", entry.StatusCode))
	case entry.StatusCode < 500:
		statusStr = logStatus4xx.Render(fmt.Sprintf("%d", entry.StatusCode))
	default:
		statusStr = logStatus5xx.Render(fmt.Sprintf("%d", entry.StatusCode))
	}

	latency := statusMuted.Render(fmt.Sprintf("%4dms", entry.Latency.Milliseconds()))

	detail := ""
	remaining := w - 55
	if remaining > 0 && entry.Detail != "" {
		detail = " " + statusMuted.Render(truncate(entry.Detail, remaining))
	}

	return fmt.Sprintf("  %s %s %s %s %s%s", ts, method, pathStr, statusStr, latency, detail)
}

func formatDuration(seconds int) string {
	d := time.Duration(seconds) * time.Second
	if d < time.Minute {
		return fmt.Sprintf("%ds", seconds)
	}
	if d < time.Hour {
		return fmt.Sprintf("%dm %ds", int(d.Minutes()), seconds%60)
	}
	h := int(d.Hours())
	m := int(d.Minutes()) % 60
	return fmt.Sprintf("%dh %dm", h, m)
}

func formatAge(seconds int64) string {
	if seconds < 60 {
		return fmt.Sprintf("%ds ago", seconds)
	}
	if seconds < 3600 {
		return fmt.Sprintf("%dm ago", seconds/60)
	}
	return fmt.Sprintf("%dh ago", seconds/3600)
}

func truncate(s string, max int) string {
	if len(s) <= max {
		return s
	}
	if max < 4 {
		return s[:max]
	}
	return s[:max-3] + "..."
}

func latencyStats(samples []float64) (min, max, avg float64) {
	if len(samples) == 0 {
		return 0, 0, 0
	}
	min = samples[0]
	max = samples[0]
	sum := 0.0
	for _, v := range samples {
		if v < min {
			min = v
		}
		if v > max {
			max = v
		}
		sum += v
	}
	avg = sum / float64(len(samples))
	return
}
