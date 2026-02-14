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
	case viewBurst:
		b.WriteString(m.renderBurstView(w, h-5))
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
		{"3", "Log", viewLog},
		{"4", "Burst Test", viewBurst},
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
		// Status
		statusStr := renderStatusBadge(hl.Status)
		lines = append(lines, labelStyle.Render("Status:")+" "+statusStr)

		// Dispenser state
		dispStr := renderDispenserState(hl.Dispenser)
		lines = append(lines, labelStyle.Render("Dispenser:")+" "+dispStr)

		// Uptime
		lines = append(lines, labelStyle.Render("Uptime:")+" "+valueBold.Render(formatDuration(hl.Uptime)))

		// Firmware
		lines = append(lines, labelStyle.Render("Firmware:")+" "+valueBold.Render(hl.Firmware))

		// Hopper
		hopperStr := statusOK.Render("● OK")
		if hl.GPIO != nil && hl.GPIO.HopperLow.Active {
			hopperStr = statusWarning.Render("⚠ LOW")
		}
		lines = append(lines, labelStyle.Render("Hopper:")+" "+hopperStr)

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
		lines = append(lines, "")
		lines = append(lines, fmt.Sprintf("  Press %s to retry", keyStyle.Render("ENTER")))
	}

	return lines
}

// --- Burst Test View ---

func (m Model) renderBurstView(w, h int) string {
	var lines []string

	lines = append(lines, sectionHeader.Render("🔥 Burst Test"))
	lines = append(lines, "")
	lines = append(lines, statusMuted.Render("  Sequential dispense requests to stress-test the dispenser"))
	lines = append(lines, "")

	if m.burst.Running {
		// Progress
		pct := 0
		if m.burst.Total > 0 {
			pct = m.burst.Completed * 100 / m.burst.Total
		}
		barWidth := 40
		filled := pct * barWidth / 100
		empty := barWidth - filled
		bar := progressFilled.Render(strings.Repeat("█", filled)) +
			progressEmpty.Render(strings.Repeat("░", empty))

		lines = append(lines, fmt.Sprintf("  [%s] %d%%", bar, pct))
		lines = append(lines, "")
		lines = append(lines, fmt.Sprintf("  Completed: %s / %d",
			valueBold.Render(fmt.Sprintf("%d", m.burst.Completed)), m.burst.Total))
		lines = append(lines, fmt.Sprintf("  Succeeded: %s", statusOK.Render(fmt.Sprintf("%d", m.burst.Succeeded))))
		lines = append(lines, fmt.Sprintf("  Failed:    %s", statusError.Render(fmt.Sprintf("%d", m.burst.Failed))))

	} else {
		// Config
		lines = append(lines, fmt.Sprintf("  Requests:       %s  %s",
			valueBold.Render(fmt.Sprintf("%-3d", m.burst.Total)),
			statusMuted.Render("(↑/↓ to adjust)")))
		lines = append(lines, fmt.Sprintf("  Tokens/request: %s  %s",
			valueBold.Render(fmt.Sprintf("%-3d", m.burst.Quantity)),
			statusMuted.Render("(←/→ to adjust)")))
		lines = append(lines, fmt.Sprintf("  Total tokens:   %s",
			coinStyle.Render(fmt.Sprintf("%d", m.burst.Total*m.burst.Quantity))))
		lines = append(lines, "")

		if m.burst.Completed > 0 {
			// Show last run results
			lines = append(lines, sectionHeader.Render("  Last Run:"))
			lines = append(lines, fmt.Sprintf("    Succeeded: %s", statusOK.Render(fmt.Sprintf("%d", m.burst.Succeeded))))
			lines = append(lines, fmt.Sprintf("    Failed:    %s", statusError.Render(fmt.Sprintf("%d", m.burst.Failed))))
			successRate := float64(m.burst.Succeeded) / float64(m.burst.Total) * 100
			lines = append(lines, fmt.Sprintf("    Rate:      %s", valueBold.Render(fmt.Sprintf("%.1f%%", successRate))))
			lines = append(lines, "")
		}

		lines = append(lines, fmt.Sprintf("  Press %s to start burst test", keyStyle.Render("ENTER")))
	}

	content := strings.Join(lines, "\n")
	panel := activePanelStyle.Width(w - 4).Render(content)

	return panel + "\n\n" + m.renderRecentLog(w-4, h-18)
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
	case viewBurst:
		pairs = append([]struct{ key, desc string }{
			{"↑↓", "count"},
			{"←→", "qty/req"},
			{"⏎", "start"},
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

	help := strings.Join(parts, statusMuted.Render(" │ "))
	sep := statusMuted.Render(strings.Repeat("─", w))
	return sep + "\n" + help
}

// --- Rendering Helpers ---

func renderStatusBadge(status string) string {
	switch status {
	case "ok":
		return statusOK.Render("● OK")
	case "degraded":
		return statusDegraded.Render("◐ DEGRADED")
	case "error":
		return statusError.Render("● ERROR")
	default:
		return statusMuted.Render("? " + status)
	}
}

func renderDispenserState(state string) string {
	switch state {
	case "idle":
		return statusOK.Render("idle")
	case "dispensing":
		return dispensingStyle.Render("⟳ dispensing")
	case "error":
		return statusError.Render("✗ error")
	default:
		return statusMuted.Render(state)
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
