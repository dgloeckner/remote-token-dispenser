package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"strings"
	"time"
)

// MaxRequestBody is the firmware's body cap (REQUEST_BODY_CAPACITY in
// firmware/dispenser/request_body.h).  A dispense request is about 40 bytes;
// past this the device answers 413 instead of truncating the body into a parse
// error that blames the JSON.
const MaxRequestBody = 256

// writeJSON writes a JSON response with the given status code
func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(v); err != nil {
		log.Printf("error encoding JSON response: %v", err)
	}
}

// signedPath is what the signature covers: the path with no query string
// (dispenser-protocol.md).
func signedPath(r *http.Request) string {
	return r.URL.Path
}

// checkSignature verifies X-Nonce / X-Signature over this request.  `body` is
// the exact bytes the caller sent — the handler has to have read them first,
// which is why the signature check on POST /dispense sits after the read.
//
// There is no X-API-Key path here.  A request carrying that header is an
// unsigned request and gets the same 401 as any other.
func (m *MockDispenser) checkSignature(r *http.Request, body string, mutating bool) AuthResult {
	return m.nonces.Verify(r.Method, signedPath(r), body,
		r.Header.Get("X-Nonce"), r.Header.Get("X-Signature"), mutating)
}

// requireSignature answers the 401 itself, with the `reason` the client acts
// on.  Returns true when the request may proceed.
func (m *MockDispenser) requireSignature(w http.ResponseWriter, r *http.Request, body string, mutating bool) bool {
	result := m.checkSignature(r, body, mutating)
	if result == ResultOK {
		return true
	}
	writeJSON(w, http.StatusUnauthorized,
		ErrorResponse{Error: "unauthorized", Reason: result.reason()})
	return false
}

// handleNonce handles GET /nonce (no auth, and there cannot be any: a client
// holds nothing to sign with until it has one).  What it hands out is 128
// random bits with a 30 s life — worth nothing without the key.
func (m *MockDispenser) handleNonce(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}
	writeJSON(w, http.StatusOK, NonceResponse{
		Nonce: m.nonces.Issue(),
		TTL:   int(NonceTTL / time.Second),
	})
}

// handleHealth handles GET /health (no auth required)
func (m *MockDispenser) handleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	log.Printf("GET /health from %s", r.RemoteAddr)

	fault, faultCode := m.GetFault()

	// TWO documents off one URL (issue #8).  Unsigned is a liveness probe and
	// answers 200, not 401: a monitor asking "is the machine there" must be
	// able to tell that from "the machine is gone", and a 401 answers neither.
	if m.checkSignature(r, "", false) != ResultOK {
		writeJSON(w, http.StatusOK, MinimalHealthResponse{
			Protocol:      m.protocol,
			State:         m.GetState(),
			Fault:         fault,
			Authenticated: false,
		})
		return
	}

	resp := HealthResponse{
		Protocol:      m.protocol,
		State:         m.GetState(),
		Authenticated: true,
		Fault:         fault,
		FaultCode:     faultCode,
		Uptime:        m.Uptime(),
		Firmware:      "mock-v1.0.0",
		// A plausible ESP8266 heap and the reset reason of a device that was
		// simply switched on.  Constant, because nothing here can leak or
		// crash — a soak run against the mock proves the RUNNER, and the
		// numbers it judges only mean something on a real board.
		HeapFree:    28000,
		ResetReason: "Power on",
		WiFi: &WiFiInfo{
			RSSI:       -55,
			IP:         "192.168.1.100",
			SSID:       "mock-network",
			Reconnects: 0,
		},
		Metrics:      m.GetMetrics(),
		ErrorHistory: m.GetErrorHistory(),
	}

	writeJSON(w, http.StatusOK, resp)
}

// handleDebug handles GET /debug (auth required): the raw pin levels, which
// left /health in issue #6.  The mock has no pins; it answers the shape so a
// client that reads them can be exercised against it.
func (m *MockDispenser) handleDebug(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}
	if !m.requireSignature(w, r, "", false) {
		return
	}
	writeJSON(w, http.StatusOK, DebugResponse{})
}

// handleDispense handles POST /dispense (auth required)
func (m *MockDispenser) handleDispense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	// The body, framed the way the firmware frames it (dispenser-protocol.md):
	// read it whole, refuse anything past the cap, and answer an empty body
	// rather than leaving the caller to time out.
	raw, err := io.ReadAll(io.LimitReader(r.Body, MaxRequestBody+1))
	if err != nil {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "invalid json"})
		return
	}
	if len(raw) > MaxRequestBody {
		writeJSON(w, http.StatusRequestEntityTooLarge, ErrorResponse{Error: "body too large"})
		return
	}
	// The signature covers the body, so it can only be checked once the body
	// has been read — and after the two framing answers above, which are the
	// cases where there are no bytes to verify.  `true`: a POST SPENDS its
	// nonce, so the identical request replayed is 401 reason=nonce.
	if !m.requireSignature(w, r, string(raw), true) {
		return
	}

	// Content type AFTER the signature, in that order in both implementations:
	// a device must not tell an unauthenticated caller which of its headers it
	// dislikes.
	contentType := r.Header.Get("Content-Type")
	if !strings.HasPrefix(contentType, "application/json") {
		writeJSON(w, http.StatusUnsupportedMediaType, ErrorResponse{Error: "content-type must be application/json"})
		return
	}

	if len(raw) == 0 {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "empty body"})
		return
	}

	var req DispenseRequest
	if err := json.Unmarshal(raw, &req); err != nil {
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

	// Idempotency check: return existing transaction if found — including the
	// one that is dispensing right now, which is the retry the terminal makes
	// when its 3 s timeout fires (see issue #2).
	if existing := m.FindTransaction(req.TxID); existing != nil {
		if existing.Quantity != req.Quantity {
			// Same id, other quantity: the caller contradicted itself. Answering
			// with the old quantity would look like a retry of a request nobody made.
			log.Printf("POST /dispense tx_id=%q rejected: known id with quantity %d, was %d",
				req.TxID, req.Quantity, existing.Quantity)
			writeJSON(w, http.StatusConflict, ErrorResponse{Error: "tx_id reused"})
			return
		}
		log.Printf("POST /dispense tx_id=%q idempotent hit, state=%s", req.TxID, existing.State)
		writeJSON(w, http.StatusOK, respondTx(existing))
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

	// The device fault: only a power cycle ends it, and the two idempotency
	// checks above have already run — a terminal asking about a transaction it
	// already sent still gets its answer (issue #6).
	if m.fault != FaultNone {
		fault, faultCode := m.fault, m.faultCode
		m.mu.Unlock()
		log.Printf("POST /dispense tx_id=%q rejected: device fault %s", req.TxID, fault)
		writeJSON(w, http.StatusConflict, ErrorResponse{
			Error:     "fault",
			Fault:     fault,
			FaultCode: faultCode,
		})
		return
	}

	// Create transaction and set as active
	tx := &Transaction{
		TxID:          req.TxID,
		State:         StateDispensing,
		Quantity:      req.Quantity,
		Dispensed:     0,
		CountReliable: true,
		ErrorType:     TxErrorNone,
		Timestamp:     time.Now(),
		StopChan:      make(chan bool, 1),
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

	// Capture the response before starting the goroutine, to avoid a data race
	resp := respondTx(tx)

	// Start scenario in a goroutine
	go m.ExecuteScenario(tx, scenario)

	writeJSON(w, http.StatusOK, resp)
}

// respondTx is the transaction body of every 200.  count_reliable,
// error_code and error_type are all REQUIRED: a reader that has to default
// one of them cannot tell "nothing went wrong" from "the device never said".
func respondTx(tx *Transaction) DispenseResponse {
	errType := tx.ErrorType
	if errType == "" {
		errType = TxErrorNone
	}
	return DispenseResponse{
		TxID:          tx.TxID,
		State:         tx.State,
		Quantity:      tx.Quantity,
		Dispensed:     tx.Dispensed,
		CountReliable: tx.CountReliable,
		ErrorCode:     tx.ErrorCode,
		ErrorType:     errType,
	}
}

// handleDispenseStatus handles GET /dispense/{tx_id} (auth required)
func (m *MockDispenser) handleDispenseStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, ErrorResponse{Error: "method not allowed"})
		return
	}

	// A status poll is read-only, so it does not spend its nonce: the same
	// one serves every poll of a transaction until it expires.
	if !m.requireSignature(w, r, "", false) {
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

	writeJSON(w, http.StatusOK, respondTx(tx))
}

// RegisterHandlers registers all HTTP handlers on the given mux.
// There is no /reset, and there must not be: a fault is ended by a power
// cycle and by nothing else (owner decision, 2026-09-20).  The mux answers
// 404 for it, which is what the conformance suite asserts.
func (m *MockDispenser) RegisterHandlers(mux *http.ServeMux) {
	mux.HandleFunc("/health", m.handleHealth)
	mux.HandleFunc("/nonce", m.handleNonce)
	mux.HandleFunc("/debug", m.handleDebug)
	mux.HandleFunc("/dispense", m.handleDispense)
	mux.HandleFunc("/dispense/", m.handleDispenseStatus)
}
