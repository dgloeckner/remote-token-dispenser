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
)

// MockDispenser manages the mock dispenser state
type MockDispenser struct {
	mu            sync.RWMutex
	startTime     time.Time
	apiKey        string
	activeTx      *Transaction
	history       []*Transaction // Ring buffer, last 8 transactions
	metrics       Metrics
	hardwareError *ErrorInfo
	errorHistory  []ErrorRecord
}

// NewMockDispenser creates a new mock dispenser
func NewMockDispenser(apiKey string) *MockDispenser {
	return &MockDispenser{
		startTime:    time.Now(),
		apiKey:       apiKey,
		history:      make([]*Transaction, 0, 8),
		errorHistory: make([]ErrorRecord, 0, 5),
		metrics: Metrics{
			TotalDispenses: 0,
			Successful:     0,
			Jams:           0,
			Partial:        0,
			Failures:       0,
		},
	}
}

// Uptime returns seconds since start
func (m *MockDispenser) Uptime() int {
	return int(time.Since(m.startTime).Seconds())
}

// GetState returns current dispenser state
func (m *MockDispenser) GetState() string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.activeTx != nil {
		return m.activeTx.State
	}
	if m.hardwareError != nil && m.hardwareError.Active {
		return StateError
	}
	return StateIdle
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
	return &txCopy
}

// AddToHistory adds a transaction to the ring buffer
func (m *MockDispenser) AddToHistory(tx *Transaction) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// Ring buffer: keep last 8
	if len(m.history) >= 8 {
		m.history = m.history[1:]
	}
	m.history = append(m.history, tx)
}

// ValidateAPIKey checks if provided key matches
func (m *MockDispenser) ValidateAPIKey(key string) bool {
	return key == m.apiKey
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

// GetHardwareError returns a copy of the hardware error if exists
func (m *MockDispenser) GetHardwareError() *ErrorInfo {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.hardwareError == nil {
		return nil
	}

	errorCopy := *m.hardwareError
	return &errorCopy
}

// GetErrorHistory returns a copy of the error history slice
func (m *MockDispenser) GetErrorHistory() []ErrorRecord {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if len(m.errorHistory) == 0 {
		return nil
	}

	historyCopy := make([]ErrorRecord, len(m.errorHistory))
	copy(historyCopy, m.errorHistory)
	return historyCopy
}
