package main

import (
	"sync"
	"time"
)

const (
	StateIdle       = "idle"
	StateDispensing = "dispensing"
	StateDone       = "done"
	StateError      = "error"
	// StateFault is a DEVICE state, not a transaction state: the machine
	// needs a human (issue #6).
	StateFault = "fault"
)

// MockDispenser manages the mock dispenser state
type MockDispenser struct {
	mu        sync.RWMutex
	startTime time.Time
	// signingKey is the shared secret.  It NEVER travels since issue #8: a
	// request carries HMAC-SHA256 over the canonical string, not the key.
	signingKey string
	nonces     *NoncePool
	// protocol is what GET /health claims.  Normally ProtocolVersion; the
	// --protocol flag sets it to something else so a terminal can be tested
	// against a device it must refuse.
	protocol     int
	activeTx     *Transaction
	history      []*Transaction // Ring buffer, last 8 transactions
	metrics      Metrics
	fault        string
	faultCode    int
	errorHistory []ErrorRecord
}

// NewMockDispenser creates a new mock dispenser claiming ProtocolVersion.
func NewMockDispenser(signingKey string) *MockDispenser {
	return NewMockDispenserWithProtocol(signingKey, ProtocolVersion)
}

// NewMockDispenserWithProtocol creates a mock that CLAIMS the given protocol
// version while behaving exactly as before.  Anything but ProtocolVersion is
// a device every conforming client has to refuse outright.
func NewMockDispenserWithProtocol(signingKey string, protocol int) *MockDispenser {
	return &MockDispenser{
		startTime:    time.Now(),
		signingKey:   signingKey,
		nonces:       NewNoncePool(signingKey),
		protocol:     protocol,
		fault:        FaultNone,
		history:      make([]*Transaction, 0, 8),
		errorHistory: make([]ErrorRecord, 0, 5),
		metrics: Metrics{
			TotalDispenses:  0,
			Successful:      0,
			Jams:            0,
			Partial:         0,
			Crashes:         0,
			Failures:        0,
			RequestedTokens: 0,
			DispensedTokens: 0,
		},
	}
}

// Uptime returns seconds since start
func (m *MockDispenser) Uptime() int {
	return int(time.Since(m.startTime).Seconds())
}

// GetState returns the DEVICE state of GET /health: idle | dispensing |
// fault.  A fault outranks everything — it is the answer to "is it safe to
// run the motor", and only a power cycle changes it (issue #6).
func (m *MockDispenser) GetState() string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.fault != FaultNone {
		return StateFault
	}
	if m.activeTx != nil {
		return StateDispensing
	}
	return StateIdle
}

// GetFault returns the device fault and its Azkoyen code.
func (m *MockDispenser) GetFault() (string, int) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.fault, m.faultCode
}

// raiseFaultLocked puts the device out of service.  INTERNAL USE ONLY -
// caller must hold m.mu.Lock().  There is no clearing counterpart, on
// purpose: the mock is restarted, which is what pulling the plug is.
func (m *MockDispenser) raiseFaultLocked(fault string, code int) {
	if m.fault == FaultNone {
		m.fault = fault
		m.faultCode = code
	}
}

// FindTransaction finds a transaction by ID in history
// Returns a copy to prevent data races
func (m *MockDispenser) FindTransaction(txID string) *Transaction {
	m.mu.RLock()
	defer m.mu.RUnlock()

	var source *Transaction

	// Check active transaction first
	if m.activeTx != nil && m.activeTx.TxID == txID {
		source = m.activeTx
	}

	// Check history if not found in active
	if source == nil {
		for _, tx := range m.history {
			if tx.TxID == txID {
				source = tx
				break
			}
		}
	}

	// Return a defensive copy to prevent data races
	if source == nil {
		return nil
	}
	txCopy := *source
	txCopy.StopChan = nil // Don't expose internal control channel
	return &txCopy
}

// AddToHistory adds a transaction to the ring buffer
func (m *MockDispenser) AddToHistory(tx *Transaction) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.addToHistoryLocked(tx)
}

// addToHistoryLocked adds a transaction to the ring buffer.
// INTERNAL USE ONLY - caller must hold m.mu.Lock()
func (m *MockDispenser) addToHistoryLocked(tx *Transaction) {
	// Ring buffer: keep last 8
	if len(m.history) >= 8 {
		m.history = m.history[1:]
	}
	m.history = append(m.history, tx)
}

// GetMetrics returns a copy of the metrics for safe concurrent access
func (m *MockDispenser) GetMetrics() Metrics {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.metrics
}

// GetActiveTxInfo returns a copy of active transaction info if exists
func (m *MockDispenser) GetActiveTxInfo() *ActiveTxInfo {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.activeTx == nil {
		return nil
	}

	return &ActiveTxInfo{
		TxID:      m.activeTx.TxID,
		Quantity:  m.activeTx.Quantity,
		Dispensed: m.activeTx.Dispensed,
	}
}

// GetErrorHistory returns a copy of the error history slice
func (m *MockDispenser) GetErrorHistory() []ErrorRecord {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if len(m.errorHistory) == 0 {
		return []ErrorRecord{}
	}

	historyCopy := make([]ErrorRecord, len(m.errorHistory))
	copy(historyCopy, m.errorHistory)
	return historyCopy
}
