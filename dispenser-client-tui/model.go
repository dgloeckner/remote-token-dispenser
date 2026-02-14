package main

import (
	"fmt"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/google/uuid"
)

// View modes
type viewMode int

const (
	viewDashboard viewMode = iota
	viewDispense
	viewTest // Renamed from viewBurst
	viewLog
)

const (
	maxLogEntries     = 100
	maxLatencySamples = 60
	healthInterval    = 5 * time.Second
	pollInterval      = 250 * time.Millisecond
)

// LogEntry represents one API call in the request log
type LogEntry struct {
	Time       time.Time
	Method     string
	Path       string
	StatusCode int
	Latency    time.Duration
	Detail     string
	IsError    bool
}

// DispenseState tracks an active dispense operation
type DispenseState struct {
	TxID      string
	Quantity  int
	Dispensed int
	State     string // "dispensing", "done", "error"
	Error     string
	StartTime time.Time
}

// TestState tracks a test cycle
type TestState struct {
	Preset      int           // 0=custom, 1=single, 2=typical, 3=stress
	CustomQty   int           // Custom quantity (1-20)
	Running     bool          // Test in progress
	LastResult  string        // Last test result message
	LastSuccess bool          // Last test succeeded
	LastTime    time.Duration // Last test duration
}

// Model is the main Bubble Tea model
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
	latencySamples []float64 // rolling latency in ms

	// Dispense state
	dispense     *DispenseState
	dispQuantity int // quantity selector (1-20)

	// Test cycle (replaces burst)
	test TestState

	// Request log
	log       []LogEntry
	logScroll int

	// Debug mode
	debugMode bool

	// UI state
	ticker int // animation frame counter
}

func NewModel(client *DispenserClient) Model {
	return Model{
		client:         client,
		mode:           viewDashboard,
		dispQuantity:   3,
		latencySamples: make([]float64, 0, maxLatencySamples),
		log:            make([]LogEntry, 0, maxLogEntries),
		test: TestState{
			Preset:    2, // Default to "typical purchase"
			CustomQty: 5,
		},
	}
}

// --- Tea messages ---

type tickMsg time.Time
type healthResultMsg struct {
	health *HealthResponse
	result APIResult
}
type dispenseStartMsg struct {
	resp   *DispenseResponse
	result APIResult
}
type dispensePollMsg struct {
	resp   *DispenseResponse
	result APIResult
}
type testCycleMsg struct {
	success bool
	message string
	elapsed time.Duration
}

// --- Commands ---

func tickCmd() tea.Cmd {
	return tea.Tick(time.Second, func(t time.Time) tea.Msg {
		return tickMsg(t)
	})
}

func (m Model) fetchHealth() tea.Cmd {
	return func() tea.Msg {
		health, result := m.client.Health()
		return healthResultMsg{health: health, result: result}
	}
}

func (m Model) startDispense() tea.Cmd {
	txID := uuid.New().String()[:8]
	qty := m.dispQuantity

	return func() tea.Msg {
		resp, result := m.client.Dispense(txID, qty)
		return dispenseStartMsg{resp: resp, result: result}
	}
}

func (m Model) pollDispense() tea.Cmd {
	if m.dispense == nil {
		return nil
	}
	txID := m.dispense.TxID

	return tea.Tick(pollInterval, func(t time.Time) tea.Msg {
		resp, result := m.client.Status(txID)
		return dispensePollMsg{resp: resp, result: result}
	})
}

func (m Model) runTestCycle() tea.Cmd {
	// TODO: Implement test cycle execution (Task 8)
	// Will run dispense based on selected preset or custom quantity
	// and return testCycleMsg with results
	return nil
}

// --- Init ---

func (m Model) Init() tea.Cmd {
	return tea.Batch(tickCmd(), m.fetchHealth())
}

// --- Update ---

func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {

	case tea.KeyMsg:
		return m.handleKey(msg)

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		return m, nil

	case tickMsg:
		m.ticker++
		var cmds []tea.Cmd
		cmds = append(cmds, tickCmd())

		// Auto-refresh health
		if time.Since(m.lastHealthAt) >= healthInterval {
			cmds = append(cmds, m.fetchHealth())
		}
		return m, tea.Batch(cmds...)

	case healthResultMsg:
		m.lastHealthAt = time.Now()
		if msg.result.Error != nil {
			m.healthErr = msg.result.Error
			m.connected = false
			m.addLog("GET", "/health", 0, msg.result.Latency, msg.result.Error.Error(), true)
		} else {
			m.health = msg.health
			m.healthErr = nil
			m.connected = true
			m.addLatency(msg.result.Latency)
			m.addLog("GET", "/health", 200, msg.result.Latency, fmt.Sprintf("status=%s dispenser=%s", msg.health.Status, msg.health.Dispenser), false)
		}
		return m, nil

	case dispenseStartMsg:
		if msg.result.Error != nil {
			m.addLog("POST", "/dispense", msg.result.StatusCode, msg.result.Latency, msg.result.Error.Error(), true)
			m.dispense = &DispenseState{
				State: "error",
				Error: msg.result.Error.Error(),
			}
			return m, nil
		}
		m.dispense = &DispenseState{
			TxID:      msg.resp.TxID,
			Quantity:  msg.resp.Quantity,
			Dispensed: msg.resp.Dispensed,
			State:     msg.resp.State,
			StartTime: time.Now(),
		}
		m.addLatency(msg.result.Latency)
		m.addLog("POST", "/dispense", 200, msg.result.Latency,
			fmt.Sprintf("tx=%s qty=%d state=%s", msg.resp.TxID, msg.resp.Quantity, msg.resp.State), false)

		if msg.resp.State == "dispensing" {
			return m, m.pollDispense()
		}
		return m, nil

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

	case testCycleMsg:
		// TODO: Implement test cycle result handling (Task 10)
		// Will update m.test.LastResult, LastSuccess, LastTime
		m.test.Running = false
		m.test.LastResult = msg.message
		m.test.LastSuccess = msg.success
		m.test.LastTime = msg.elapsed
		return m, m.fetchHealth()

	}

	return m, nil
}

func (m *Model) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	key := msg.String()

	// Global keys
	switch key {
	case "q", "ctrl+c":
		m.quitting = true
		return m, tea.Quit

	case "1":
		m.mode = viewDashboard
		return m, nil
	case "2":
		m.mode = viewDispense
		return m, nil
	case "3":
		m.mode = viewTest
		return m, nil
	case "4":
		m.mode = viewLog
		return m, nil

	case "r", "R":
		return m, m.fetchHealth()

	case "d", "D":
		m.debugMode = !m.debugMode
		return m, nil
	}

	// Mode-specific keys
	switch m.mode {
	case viewDispense:
		return m.handleDispenseKeys(key)
	case viewTest:
		return m.handleTestKeys(key)
	case viewLog:
		return m.handleLogKeys(key)
	}

	return m, nil
}

func (m *Model) handleDispenseKeys(key string) (tea.Model, tea.Cmd) {
	switch key {
	case "up", "k":
		if m.dispQuantity < 20 {
			m.dispQuantity++
		}
	case "down", "j":
		if m.dispQuantity > 1 {
			m.dispQuantity--
		}
	case "enter":
		if m.dispense == nil || m.dispense.State != "dispensing" {
			m.dispense = nil
			return m, m.startDispense()
		}
	}
	return m, nil
}

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

func (m *Model) handleLogKeys(key string) (tea.Model, tea.Cmd) {
	switch key {
	case "up", "k":
		if m.logScroll > 0 {
			m.logScroll--
		}
	case "down", "j":
		if m.logScroll < len(m.log)-1 {
			m.logScroll++
		}
	case "G":
		m.logScroll = max(0, len(m.log)-1)
	case "g":
		m.logScroll = 0
	case "c", "C":
		m.log = m.log[:0]
		m.logScroll = 0
	}
	return m, nil
}

// --- Helpers ---

func (m *Model) addLog(method, path string, status int, latency time.Duration, detail string, isError bool) {
	entry := LogEntry{
		Time:       time.Now(),
		Method:     method,
		Path:       path,
		StatusCode: status,
		Latency:    latency,
		Detail:     detail,
		IsError:    isError,
	}
	m.log = append(m.log, entry)
	if len(m.log) > maxLogEntries {
		m.log = m.log[1:]
	}
	// Auto-scroll to bottom
	m.logScroll = max(0, len(m.log)-1)
}

func (m *Model) addLatency(d time.Duration) {
	ms := float64(d.Microseconds()) / 1000.0
	m.latencySamples = append(m.latencySamples, ms)
	if len(m.latencySamples) > maxLatencySamples {
		m.latencySamples = m.latencySamples[1:]
	}
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
