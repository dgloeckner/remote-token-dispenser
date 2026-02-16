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
func (m *MockDispenser) FindTransaction(txID string) *Transaction {
	m.mu.RLock()
	defer m.mu.RUnlock()

	// Check active transaction first
	if m.activeTx != nil && m.activeTx.TxID == txID {
		return m.activeTx
	}

	// Check history
	for _, tx := range m.history {
		if tx.TxID == txID {
			return tx
		}
	}

	return nil
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
