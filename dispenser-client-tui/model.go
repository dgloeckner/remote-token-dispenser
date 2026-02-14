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
	viewLog
	viewBurst
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

// BurstState tracks a burst test
type BurstState struct {
	Total     int
	Completed int
	Succeeded int
	Failed    int
	Running   bool
	Quantity  int // tokens per request
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

	// Burst test
	burst BurstState

	// Request log
	log       []LogEntry
	logScroll int

	// UI state
	ticker   int // animation frame counter
}

func NewModel(client *DispenserClient) Model {
	return Model{
		client:         client,
		mode:           viewDashboard,
		dispQuantity:   3,
		latencySamples: make([]float64, 0, maxLatencySamples),
		log:            make([]LogEntry, 0, maxLogEntries),
		burst: BurstState{
			Total:    10,
			Quantity: 1,
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
type burstStepMsg struct {
	resp   *DispenseResponse
	result APIResult
	index  int
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

func (m Model) burstStep(index int) tea.Cmd {
	qty := m.burst.Quantity
	return func() tea.Msg {
		txID := uuid.New().String()[:8]
		resp, result := m.client.Dispense(txID, qty)

		// If started, poll until done
		if resp != nil && resp.State == "dispensing" {
			for i := 0; i < 120; i++ { // max 30s
				time.Sleep(pollInterval)
				status, _ := m.client.Status(resp.TxID)
				if status != nil && status.State != "dispensing" {
					resp = status
					break
				}
			}
		}

		return burstStepMsg{resp: resp, result: result, index: index}
	}
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
		// Done or error - stop polling, refresh health
		return m, m.fetchHealth()

	case burstStepMsg:
		m.burst.Completed++
		if msg.result.Error != nil {
			m.burst.Failed++
			m.addLog("POST", "/dispense", msg.result.StatusCode, msg.result.Latency, msg.result.Error.Error(), true)
		} else {
			if msg.resp != nil && msg.resp.State == "done" {
				m.burst.Succeeded++
			} else {
				m.burst.Failed++
			}
			if msg.resp != nil {
				m.addLog("POST", "/dispense", 200, msg.result.Latency,
					fmt.Sprintf("burst[%d] tx=%s state=%s", msg.index+1, msg.resp.TxID, msg.resp.State), false)
			}
		}

		// Launch next burst step if more remain
		if m.burst.Completed < m.burst.Total {
			return m, m.burstStep(m.burst.Completed)
		}
		m.burst.Running = false
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
		m.mode = viewLog
		return m, nil
	case "4":
		m.mode = viewBurst
		return m, nil

	case "r", "R":
		return m, m.fetchHealth()
	}

	// Mode-specific keys
	switch m.mode {
	case viewDispense:
		return m.handleDispenseKeys(key)
	case viewBurst:
		return m.handleBurstKeys(key)
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

func (m *Model) handleBurstKeys(key string) (tea.Model, tea.Cmd) {
	if m.burst.Running {
		return m, nil
	}
	switch key {
	case "up", "k":
		if m.burst.Total < 50 {
			m.burst.Total++
		}
	case "down", "j":
		if m.burst.Total > 1 {
			m.burst.Total--
		}
	case "left", "h":
		if m.burst.Quantity > 1 {
			m.burst.Quantity--
		}
	case "right", "l":
		if m.burst.Quantity < 10 {
			m.burst.Quantity++
		}
	case "enter":
		m.burst.Running = true
		m.burst.Completed = 0
		m.burst.Succeeded = 0
		m.burst.Failed = 0
		return m, m.burstStep(0)
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
