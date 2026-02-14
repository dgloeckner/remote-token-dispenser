package main

import "github.com/charmbracelet/lipgloss"

// k9s-inspired color palette
var (
	colorPrimary   = lipgloss.Color("#7C3AED") // purple
	colorSecondary = lipgloss.Color("#06B6D4") // cyan
	colorSuccess   = lipgloss.Color("#10B981") // green
	colorWarning   = lipgloss.Color("#F59E0B") // amber
	colorError     = lipgloss.Color("#EF4444") // red
	colorMuted     = lipgloss.Color("#6B7280") // gray
	colorText      = lipgloss.Color("#F9FAFB") // white
	colorDim       = lipgloss.Color("#9CA3AF") // light gray
	colorBg        = lipgloss.Color("#111827") // dark bg
	colorPanelBg   = lipgloss.Color("#1F2937") // panel bg
	colorBorder    = lipgloss.Color("#374151") // border
	colorCoin      = lipgloss.Color("#FBBF24") // gold for coins!
)

var (
	// Title bar
	titleStyle = lipgloss.NewStyle().
			Bold(true).
			Foreground(lipgloss.Color("#FAFAFA")).
			Background(colorPrimary).
			Padding(0, 1)

	// Panel borders
	panelStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(colorBorder).
			Padding(0, 1)

	activePanelStyle = lipgloss.NewStyle().
				Border(lipgloss.RoundedBorder()).
				BorderForeground(colorPrimary).
				Padding(0, 1)

	// Labels
	labelStyle = lipgloss.NewStyle().
			Foreground(colorDim).
			Width(18)

	valueBold = lipgloss.NewStyle().
			Bold(true).
			Foreground(colorText)

	// Status indicators
	statusOK = lipgloss.NewStyle().
			Foreground(colorSuccess).
			Bold(true)

	statusDegraded = lipgloss.NewStyle().
			Foreground(colorWarning).
			Bold(true)

	statusError = lipgloss.NewStyle().
			Foreground(colorError).
			Bold(true)

	statusWarning = lipgloss.NewStyle().
			Foreground(colorWarning).
			Bold(true)

	statusMuted = lipgloss.NewStyle().
			Foreground(colorMuted)

	// Coin art
	coinStyle = lipgloss.NewStyle().
			Foreground(colorCoin).
			Bold(true)

	// Key hints
	keyStyle = lipgloss.NewStyle().
			Foreground(colorSecondary).
			Bold(true)

	descStyle = lipgloss.NewStyle().
			Foreground(colorDim)

	// Log entries
	logTimestamp = lipgloss.NewStyle().
			Foreground(colorMuted)

	logMethod = lipgloss.NewStyle().
			Foreground(colorSecondary).
			Bold(true).
			Width(5)

	logPath = lipgloss.NewStyle().
			Foreground(colorText)

	logStatus200 = lipgloss.NewStyle().
			Foreground(colorSuccess).
			Bold(true)

	logStatus4xx = lipgloss.NewStyle().
			Foreground(colorWarning).
			Bold(true)

	logStatus5xx = lipgloss.NewStyle().
			Foreground(colorError).
			Bold(true)

	// Sparkline
	sparkStyle = lipgloss.NewStyle().
			Foreground(colorSecondary)

	sparkHighStyle = lipgloss.NewStyle().
			Foreground(colorWarning)

	// Progress bar
	progressFilled = lipgloss.NewStyle().
			Foreground(colorCoin).
			Bold(true)

	progressEmpty = lipgloss.NewStyle().
			Foreground(colorBorder)

	// Section headers
	sectionHeader = lipgloss.NewStyle().
			Foreground(colorPrimary).
			Bold(true).
			MarginBottom(0)

	// Dispense animation
	dispensingStyle = lipgloss.NewStyle().
			Foreground(colorCoin).
			Bold(true).
			Blink(true)

	errorStyle = lipgloss.NewStyle().
			Foreground(colorError).
			Bold(true)
)
