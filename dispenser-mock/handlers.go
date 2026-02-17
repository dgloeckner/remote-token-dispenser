package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strings"
	"time"
)

// writeJSON writes a JSON response with the given status code
func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(v); err != nil {
		log.Printf("error encoding JSON response: %v", err)
	}
}

// requireAPIKey validates the X-API-Key header and writes a 401 if invalid.
// Returns true if the key is valid.
func (m *MockDispenser) requireAPIKey(w http.ResponseWriter, r *http.Request) bool {
	key := r.Header.Get("X-API-Key")
	if !m.ValidateAPIKey(key) {
		writeJSON(w, http.StatusUnauthorized, ErrorResponse{Error: "unauthorized"})
		return false
	}
	return true
}

// handleHealth handles GET /health (no auth required)
func (m *MockDispenser) handleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	log.Printf("GET /health from %s", r.RemoteAddr)

	dispenserState := m.GetState()

	var activeTx *ActiveTxInfo
	if dispenserState == StateDispensing {
		activeTx = m.GetActiveTxInfo()
	}

	errorInfo := m.GetHardwareError()
	if errorInfo == nil {
		errorInfo = &ErrorInfo{Active: false}
	}

	resp := HealthResponse{
		Status:   "ok",
		Uptime:   m.Uptime(),
		Firmware: "mock-v1.0.0",
		WiFi: &WiFiInfo{
			RSSI: -55,
			IP:   "192.168.1.100",
			SSID: "mock-network",
		},
		Dispenser: dispenserState,
		GPIO: &GPIOInfo{},
		Metrics:      m.GetMetrics(),
		Error:        errorInfo,
		ErrorHistory: m.GetErrorHistory(),
	}

	// Populate active tx into metrics display if dispensing
	if activeTx != nil {
		_ = activeTx // already embedded via Metrics; made available for future use
	}

	writeJSON(w, http.StatusOK, resp)
}

// handleDispense handles POST /dispense (auth required)
func (m *MockDispenser) handleDispense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	if !m.requireAPIKey(w, r) {
		return
	}

	contentType := r.Header.Get("Content-Type")
	if !strings.HasPrefix(contentType, "application/json") {
		writeJSON(w, http.StatusUnsupportedMediaType, ErrorResponse{Error: "content-type must be application/json"})
		return
	}

	var req DispenseRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: fmt.Sprintf("invalid JSON: %v", err)})
		return
	}

	log.Printf("POST /dispense tx_id=%q quantity=%d from %s", req.TxID, req.Quantity, r.RemoteAddr)

	// Validate tx_id: 1-16 chars
	if len(req.TxID) == 0 || len(req.TxID) > 16 {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "tx_id must be 1-16 characters"})
		return
	}

	// Validate quantity: 1-20
	if req.Quantity < 1 || req.Quantity > 20 {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "quantity must be between 1 and 20"})
		return
	}

	// Idempotency check: return existing transaction if found
	if existing := m.FindTransaction(req.TxID); existing != nil {
		log.Printf("POST /dispense tx_id=%q idempotent hit, state=%s", req.TxID, existing.State)
		writeJSON(w, http.StatusOK, DispenseResponse{
			TxID:      existing.TxID,
			State:     existing.State,
			Quantity:  existing.Quantity,
			Dispensed: existing.Dispensed,
		})
		return
	}

	// Check busy state and hardware error under lock so we can also set activeTx atomically
	m.mu.Lock()

	if m.activeTx != nil {
		busy := m.activeTx
		m.mu.Unlock()
		log.Printf("POST /dispense tx_id=%q rejected: busy with %s", req.TxID, busy.TxID)
		writeJSON(w, http.StatusConflict, ErrorResponse{
			Error:       "busy",
			ActiveTxID:  busy.TxID,
			ActiveState: busy.State,
		})
		return
	}

	if m.hardwareError != nil && m.hardwareError.Active {
		m.mu.Unlock()
		log.Printf("POST /dispense tx_id=%q rejected: hardware error active", req.TxID)
		writeJSON(w, http.StatusConflict, ErrorResponse{
			Error: "error",
		})
		return
	}

	// Create transaction and set as active
	tx := &Transaction{
		TxID:      req.TxID,
		State:     StateDispensing,
		Quantity:  req.Quantity,
		Dispensed: 0,
		Timestamp: time.Now(),
		StopChan:  make(chan bool, 1),
	}
	m.activeTx = tx

	m.mu.Unlock()

	scenario := GetScenarioForQuantity(req.Quantity)
	log.Printf("POST /dispense tx_id=%q scenario=%s", req.TxID, scenario)

	// Special case: crash_after_first closes the connection mid-response
	if scenario == "crash_after_first" {
		hijacker, ok := w.(http.Hijacker)
		if !ok {
			log.Printf("POST /dispense tx_id=%q crash scenario: server does not support hijacking", req.TxID)
			// Fall through to normal execution if hijacking is not supported
		} else {
			// Start the scenario in the background so it can dispense 1 token
			go m.ExecuteScenario(tx, scenario)

			// Give the scenario a moment to dispense the first token
			time.Sleep(150 * time.Millisecond)

			// Abruptly close the connection
			conn, _, err := hijacker.Hijack()
			if err != nil {
				log.Printf("POST /dispense tx_id=%q crash scenario: hijack error: %v", req.TxID, err)
			} else {
				log.Printf("POST /dispense tx_id=%q crash scenario: closing connection", req.TxID)
				conn.Close()
			}
			return
		}
	}

	// Start scenario in a goroutine
	go m.ExecuteScenario(tx, scenario)

	writeJSON(w, http.StatusOK, DispenseResponse{
		TxID:      tx.TxID,
		State:     tx.State,
		Quantity:  tx.Quantity,
		Dispensed: tx.Dispensed,
	})
}

// handleDispenseStatus handles GET /dispense/{tx_id} (auth required)
func (m *MockDispenser) handleDispenseStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	if !m.requireAPIKey(w, r) {
		return
	}

	// Extract tx_id from URL path: /dispense/{tx_id}
	path := strings.TrimPrefix(r.URL.Path, "/dispense/")
	txID := strings.TrimSpace(path)
	if txID == "" {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "missing tx_id in URL path"})
		return
	}

	log.Printf("GET /dispense/%s from %s", txID, r.RemoteAddr)

	tx := m.FindTransaction(txID)
	if tx == nil {
		writeJSON(w, http.StatusNotFound, ErrorResponse{Error: "not found"})
		return
	}

	writeJSON(w, http.StatusOK, DispenseResponse{
		TxID:      tx.TxID,
		State:     tx.State,
		Quantity:  tx.Quantity,
		Dispensed: tx.Dispensed,
	})
}

// RegisterHandlers registers all HTTP handlers on the given mux
func (m *MockDispenser) RegisterHandlers(mux *http.ServeMux) {
	mux.HandleFunc("/health", m.handleHealth)
	mux.HandleFunc("/dispense", m.handleDispense)
	mux.HandleFunc("/dispense/", m.handleDispenseStatus)
}
