package main

import (
	"time"
)

// GetScenarioForQuantity determines which scenario to run based on quantity
func GetScenarioForQuantity(quantity int) string {
	switch quantity {
	case 1, 2, 3:
		return "success"
	case 4:
		return "timeout_partial"
	case 5:
		return "crash_after_first"
	case 6:
		return "partial_dispense"
	case 7:
		return "load_delay"
	case 8:
		return "error_coin_stuck"
	case 9:
		return "error_sensor_off"
	case 10:
		return "error_jam_permanent"
	case 11:
		return "error_max_span"
	case 12:
		return "error_motor_fault"
	case 13:
		return "error_sensor_fault"
	case 14:
		return "error_power_fault"
	case 15:
		return "slow_dispense"
	default:
		if quantity >= 16 && quantity <= 20 {
			return "success"
		}
		return "invalid"
	}
}

// ExecuteScenario runs the appropriate scenario
func (m *MockDispenser) ExecuteScenario(tx *Transaction, scenario string) {
	switch scenario {
	case "success":
		m.executeSuccess(tx, 100*time.Millisecond)
	case "timeout_partial":
		m.executeTimeoutPartial(tx)
	case "crash_after_first":
		m.executeCrashAfterFirst(tx)
	case "partial_dispense":
		m.executePartialDispense(tx)
	case "load_delay":
		m.executeLoadDelay(tx)
	case "slow_dispense":
		m.executeSuccess(tx, 500*time.Millisecond)
	case "error_coin_stuck":
		m.executeHardwareError(tx, 1, "COIN_STUCK", "Coin stuck in exit sensor (>65ms)")
	case "error_sensor_off":
		m.executeHardwareError(tx, 2, "SENSOR_OFF", "Exit sensor stuck OFF")
	case "error_jam_permanent":
		m.executeHardwareError(tx, 3, "JAM_PERMANENT", "Permanent jam detected")
	case "error_max_span":
		m.executeHardwareError(tx, 4, "MAX_SPAN", "Multiple spans exceeded max time")
	case "error_motor_fault":
		m.executeHardwareError(tx, 5, "MOTOR_FAULT", "Motor doesn't start")
	case "error_sensor_fault":
		m.executeHardwareError(tx, 6, "SENSOR_FAULT", "Exit sensor disconnected/faulty")
	case "error_power_fault":
		m.executeHardwareError(tx, 7, "POWER_FAULT", "Power supply out of range")
	}
}

// executeSuccess simulates successful dispense
func (m *MockDispenser) executeSuccess(tx *Transaction, delay time.Duration) {
	m.mu.Lock()
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.mu.Unlock()

	ticker := time.NewTicker(delay)
	defer ticker.Stop()

	for {
		select {
		case <-tx.StopChan:
			return
		case <-ticker.C:
			m.mu.Lock()
			tx.Dispensed++
			if tx.Dispensed >= tx.Quantity {
				tx.State = StateDone
				m.metrics.Successful++
				m.metrics.DispensedTokens += tx.Dispensed
				m.activeTx = nil
				m.addToHistoryLocked(tx)
				m.mu.Unlock()
				return
			}
			m.mu.Unlock()
		}
	}
}

// executeTimeoutPartial simulates timeout after 2 tokens
func (m *MockDispenser) executeTimeoutPartial(tx *Transaction) {
	m.mu.Lock()
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.mu.Unlock()

	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	// Dispense 2 tokens
	for i := 0; i < 2; i++ {
		select {
		case <-tx.StopChan:
			return
		case <-ticker.C:
			m.mu.Lock()
			tx.Dispensed++
			m.mu.Unlock()
		}
	}

	// Wait 5 seconds (jam timeout)
	timeout := time.NewTimer(5 * time.Second)
	defer timeout.Stop()

	select {
	case <-tx.StopChan:
		return
	case <-timeout.C:
		// Enter error state
		m.mu.Lock()
		tx.State = StateError
		m.metrics.Jams++
		m.metrics.Partial++
		m.metrics.Failures++
		m.metrics.DispensedTokens += tx.Dispensed
		m.activeTx = nil
		m.addToHistoryLocked(tx)
		m.mu.Unlock()
	}
}

// executeCrashAfterFirst simulates crash (closes connection)
func (m *MockDispenser) executeCrashAfterFirst(tx *Transaction) {
	m.mu.Lock()
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.metrics.Crashes++
	m.mu.Unlock()

	// Dispense 1 token
	timer := time.NewTimer(100 * time.Millisecond)
	defer timer.Stop()

	select {
	case <-tx.StopChan:
		return
	case <-timer.C:
		m.mu.Lock()
		tx.Dispensed++
		m.metrics.DispensedTokens += tx.Dispensed
		m.mu.Unlock()
	}

	// Simulate ESP8266 restart after crash: brief delay, then lose all state.
	// Real hardware: MCU resets, all RAM is lost. The terminal will receive 404
	// on subsequent GET /dispense/{txId} calls, triggering manual reconciliation.
	// Note: Connection is closed by the handler (hijack) before we reach here.
	time.Sleep(2 * time.Second)
	m.mu.Lock()
	m.activeTx = nil // Clear without adding to history — restart loses all state
	m.mu.Unlock()
}

// executePartialDispense dispenses 4 of 6, then jam
func (m *MockDispenser) executePartialDispense(tx *Transaction) {
	m.mu.Lock()
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.mu.Unlock()

	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	// Dispense 4 tokens
	for i := 0; i < 4; i++ {
		select {
		case <-tx.StopChan:
			return
		case <-ticker.C:
			m.mu.Lock()
			tx.Dispensed++
			m.mu.Unlock()
		}
	}

	// Enter error state
	m.mu.Lock()
	tx.State = StateError
	m.metrics.Jams++
	m.metrics.Partial++
	m.metrics.Failures++
	m.metrics.DispensedTokens += tx.Dispensed
	m.activeTx = nil
	m.addToHistoryLocked(tx)
	m.mu.Unlock()
}

// executeLoadDelay simulates empty hopper load time
func (m *MockDispenser) executeLoadDelay(tx *Transaction) {
	m.mu.Lock()
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.mu.Unlock()

	// First token: 2.5s delay
	time.Sleep(2500 * time.Millisecond)
	m.mu.Lock()
	tx.Dispensed++
	m.mu.Unlock()

	// Remaining tokens: 100ms each
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-tx.StopChan:
			return
		case <-ticker.C:
			m.mu.Lock()
			tx.Dispensed++
			if tx.Dispensed >= tx.Quantity {
				tx.State = StateDone
				m.metrics.Successful++
				m.metrics.DispensedTokens += tx.Dispensed
				m.activeTx = nil
				m.addToHistoryLocked(tx)
				m.mu.Unlock()
				return
			}
			m.mu.Unlock()
		}
	}
}

// executeHardwareError simulates Azkoyen error code
func (m *MockDispenser) executeHardwareError(tx *Transaction, code int, errType, description string) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// Set hardware error
	m.hardwareError = &ErrorInfo{
		Active:      true,
		Code:        code,
		Type:        errType,
		Timestamp:   int(m.Uptime()),
		Description: description,
	}

	// Add to error history
	if len(m.errorHistory) >= 5 {
		m.errorHistory = m.errorHistory[1:]
	}
	m.errorHistory = append(m.errorHistory, ErrorRecord{
		Code:      code,
		Type:      errType,
		Timestamp: int(m.Uptime()),
		Cleared:   false,
	})

	// Mark transaction as error
	tx.State = StateError
	m.metrics.TotalDispenses++
	m.metrics.RequestedTokens += tx.Quantity
	m.metrics.Failures++
	// No dispensed tokens - error occurred immediately
	m.activeTx = nil
	m.addToHistoryLocked(tx)
}
