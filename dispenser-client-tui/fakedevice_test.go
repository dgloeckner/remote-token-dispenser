package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"time"
)

// fakeDevice is a minimal, protocol-2-conforming dispenser.  It exists so the
// conformance suite itself is under test: a case that passes against everything
// is not a case.  Deliberately independent of both the Go mock and the
// firmware — those are what the suite judges.
type fakeDevice struct {
	mu sync.Mutex

	apiKey string
	// knobs for the negative tests
	protocol        int
	ignoreAuth      bool
	rejectRetry     bool // answer 409 to a retry of the active tx (the #2 bug)
	acceptReusedQty bool // answer 200 to a known tx_id with another quantity
	orphanOnReplay  bool // let an idempotent hit take the active tx with it

	active  *fakeTx
	history map[string]*fakeTx
	errored bool
}

type fakeTx struct {
	ID        string
	State     string
	Quantity  int
	Dispensed int
}

func newFakeDevice(apiKey string) *fakeDevice {
	return &fakeDevice{apiKey: apiKey, protocol: ProtocolVersion, history: map[string]*fakeTx{}}
}

func (f *fakeDevice) server() *httptest.Server {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", f.health)
	mux.HandleFunc("/dispense", f.dispense)
	mux.HandleFunc("/dispense/", f.status)
	return httptest.NewServer(mux)
}

func (f *fakeDevice) writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func (f *fakeDevice) authed(r *http.Request) bool {
	return f.ignoreAuth || r.Header.Get("X-API-Key") == f.apiKey
}

func (f *fakeDevice) health(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	state := "idle"
	if f.active != nil {
		state = f.active.State
	} else if f.errored {
		state = "error"
	}
	errActive := f.errored
	proto := f.protocol
	f.mu.Unlock()

	f.writeJSON(w, 200, map[string]any{
		"protocol":  proto,
		"status":    "ok",
		"uptime":    42,
		"firmware":  "fake-device",
		"dispenser": state,
		"metrics":   map[string]int{"total_dispenses": 0},
		"error":     map[string]any{"active": errActive},
	})
}

func (f *fakeDevice) dispense(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		f.writeJSON(w, 405, map[string]string{"error": "method not allowed"})
		return
	}
	if !f.authed(r) {
		f.writeJSON(w, 401, map[string]string{"error": "unauthorized"})
		return
	}
	if !strings.HasPrefix(r.Header.Get("Content-Type"), "application/json") {
		f.writeJSON(w, 415, map[string]string{"error": "content-type must be application/json"})
		return
	}
	var req DispenseRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		f.writeJSON(w, 400, map[string]string{"error": "invalid json"})
		return
	}
	if len(req.TxID) == 0 || len(req.TxID) > 16 || req.Quantity < 1 || req.Quantity > 20 {
		f.writeJSON(w, 400, map[string]string{"error": "invalid tx_id or quantity"})
		return
	}

	f.mu.Lock()
	defer f.mu.Unlock()

	if f.active != nil && f.active.ID == req.TxID {
		if f.rejectRetry {
			f.writeJSON(w, 409, map[string]string{"error": "busy", "active_tx_id": f.active.ID})
			return
		}
		if f.active.Quantity != req.Quantity && !f.acceptReusedQty {
			f.writeJSON(w, 409, map[string]string{"error": "tx_id reused"})
			return
		}
		f.writeJSON(w, 200, f.respond(f.active))
		return
	}
	if tx, ok := f.history[req.TxID]; ok {
		if tx.Quantity != req.Quantity && !f.acceptReusedQty {
			f.writeJSON(w, 409, map[string]string{"error": "tx_id reused"})
			return
		}
		if f.orphanOnReplay {
			f.active = tx // what the firmware did before #2
		}
		f.writeJSON(w, 200, f.respond(tx))
		return
	}
	if f.active != nil {
		f.writeJSON(w, 409, map[string]string{"error": "busy", "active_tx_id": f.active.ID})
		return
	}
	if f.errored {
		f.writeJSON(w, 409, map[string]string{"error": "error"})
		return
	}

	tx := &fakeTx{ID: req.TxID, State: "dispensing", Quantity: req.Quantity}
	f.active = tx

	// Quantity 8 is the hardware-error scenario, mirroring the Go mock.
	if req.Quantity == 8 {
		tx.State = "error"
		f.errored = true
		f.active = nil
		f.history[tx.ID] = tx
		f.writeJSON(w, 200, f.respond(tx))
		return
	}

	go f.run(tx)
	f.writeJSON(w, 200, f.respond(tx))
}

// run dispenses one token every 20ms.
func (f *fakeDevice) run(tx *fakeTx) {
	for i := 0; i < tx.Quantity; i++ {
		time.Sleep(20 * time.Millisecond)
		f.mu.Lock()
		tx.Dispensed++
		f.mu.Unlock()
	}
	f.mu.Lock()
	tx.State = "done"
	f.history[tx.ID] = tx
	f.active = nil
	f.mu.Unlock()
}

func (f *fakeDevice) status(w http.ResponseWriter, r *http.Request) {
	if !f.authed(r) {
		f.writeJSON(w, 401, map[string]string{"error": "unauthorized"})
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/dispense/")

	f.mu.Lock()
	defer f.mu.Unlock()
	if f.active != nil && f.active.ID == id {
		f.writeJSON(w, 200, f.respond(f.active))
		return
	}
	if tx, ok := f.history[id]; ok {
		f.writeJSON(w, 200, f.respond(tx))
		return
	}
	f.writeJSON(w, 404, map[string]string{"error": "not found"})
}

// respond must be called with f.mu held.
func (f *fakeDevice) respond(tx *fakeTx) map[string]any {
	return map[string]any{
		"tx_id":     tx.ID,
		"state":     tx.State,
		"quantity":  tx.Quantity,
		"dispensed": tx.Dispensed,
	}
}

var _ = fmt.Sprintf
