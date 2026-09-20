package main

import (
	"encoding/json"
	"fmt"
	"io"
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
	protocol          int
	ignoreAuth        bool
	rejectRetry       bool // answer 409 to a retry of the active tx (the #2 bug)
	acceptReusedQty   bool // answer 200 to a known tx_id with another quantity
	orphanOnReplay    bool // let an idempotent hit take the active tx with it
	omitCountReliable bool // leave count_reliable out of the response (#3)
	forgetCrashedTx   bool // lose the crashed transaction on reboot, as before #3
	claimCountExact   bool // claim count_reliable=true even after a power loss

	// Issue #4: what the async callback costs the caller.
	emptyBodyDelay time.Duration // make an empty body wait instead of answering 400
	truncateBody   int           // parse only the first N bytes, as a body callback
	//                              that ignores index/total parses one chunk
	postDelay time.Duration // do the work (flash erase, 500 bytes of serial) inline
	noBodyCap bool          // accept a body of any size instead of answering 413

	// Issue #5: what happens to a token that falls after the motor stop.
	clampDispensed    bool // never report more than quantity, as the firmware did
	omitOverrunMetric bool // leave metrics.overrun_tokens out of /health

	// Issue #6: the fault model.
	acceptWhileFaulted bool // take a new transaction although a fault is up
	legacyHealthShape  bool // send protocol 1's status/dispenser pair instead
	publishHopperLow   bool // publish the empty sensor that never worked
	omitErrorCode      bool // leave error_code/error_type off transactions
	faultAfterReset    bool // report a recovered crash as a device fault
	servesReset        bool // offer a way out of a fault that is not a power cycle

	active    *fakeTx
	history   map[string]*fakeTx
	fault     string
	faultCode int
	overruns  int
}

type fakeTx struct {
	ID        string
	State     string
	Quantity  int
	Dispensed int
	Reliable  bool
	ErrorCode int
	ErrorType string
}

// fakeMaxBody mirrors REQUEST_BODY_CAPACITY in the firmware.
const fakeMaxBody = 256

func newFakeDevice(apiKey string) *fakeDevice {
	return &fakeDevice{apiKey: apiKey, protocol: ProtocolVersion, fault: "none",
		history: map[string]*fakeTx{}}
}

func (f *fakeDevice) server() *httptest.Server {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", f.health)
	mux.HandleFunc("/dispense", f.dispense)
	mux.HandleFunc("/dispense/", f.status)
	mux.HandleFunc("/debug", f.debug)
	if f.servesReset {
		mux.HandleFunc("/reset", func(w http.ResponseWriter, r *http.Request) {
			f.mu.Lock()
			f.fault, f.faultCode = "none", 0
			f.mu.Unlock()
			f.writeJSON(w, 200, map[string]string{"state": "idle"})
		})
	}
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
	if f.fault != "none" {
		state = "fault"
	} else if f.active != nil {
		state = "dispensing"
	}
	fault, faultCode := f.fault, f.faultCode
	proto := f.protocol
	overruns := f.overruns
	f.mu.Unlock()

	metrics := map[string]int{"total_dispenses": 0}
	if !f.omitOverrunMetric {
		metrics["overrun_tokens"] = overruns
	}

	body := map[string]any{
		"protocol":   proto,
		"uptime":     42,
		"firmware":   "fake-device",
		"state":      state,
		"fault":      fault,
		"fault_code": faultCode,
		"metrics":    metrics,
	}
	if f.legacyHealthShape {
		// Protocol 1: two overlapping fields and no fault at all.
		delete(body, "state")
		delete(body, "fault")
		delete(body, "fault_code")
		body["status"] = "ok"
		body["dispenser"] = state
	}
	if f.publishHopperLow {
		body["gpio"] = map[string]any{
			"hopper_low": map[string]any{"raw": 1, "active": false},
		}
	}
	f.writeJSON(w, 200, body)
}

func (f *fakeDevice) debug(w http.ResponseWriter, r *http.Request) {
	if !f.authed(r) {
		f.writeJSON(w, 401, map[string]string{"error": "unauthorized"})
		return
	}
	f.writeJSON(w, 200, map[string]any{
		"gpio": map[string]any{
			"coin_pulse":   map[string]any{"raw": 1, "active": false},
			"error_signal": map[string]any{"raw": 1, "active": false},
		},
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
	raw, err := io.ReadAll(r.Body)
	if err != nil {
		f.writeJSON(w, 400, map[string]string{"error": "invalid json"})
		return
	}
	if len(raw) == 0 {
		// A device that answers this one late (or not at all) is the bug of
		// issue #4: the request handler was an empty lambda.
		time.Sleep(f.emptyBodyDelay)
		f.writeJSON(w, 400, map[string]string{"error": "empty body"})
		return
	}
	if len(raw) > fakeMaxBody && !f.noBodyCap {
		f.writeJSON(w, 413, map[string]string{"error": "body too large"})
		return
	}
	if f.truncateBody > 0 && f.truncateBody < len(raw) {
		raw = raw[:f.truncateBody]
	}
	var req DispenseRequest
	if err := json.Unmarshal(raw, &req); err != nil {
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
	if f.fault != "none" && !f.acceptWhileFaulted {
		f.writeJSON(w, 409, map[string]any{"error": "fault",
			"fault": f.fault, "fault_code": f.faultCode})
		return
	}

	// The work the firmware used to do in the TCP callback, before the caller
	// got its answer: a flash sector erase plus ~500 bytes at 9600 baud.
	time.Sleep(f.postDelay)

	tx := &fakeTx{ID: req.TxID, State: "dispensing", Quantity: req.Quantity, Reliable: true,
		ErrorType: "NONE"}
	f.active = tx

	// The hopper-error scenario, mirroring the Go mock: a decoded error faults
	// the device, and only a power cycle ends that (issue #6).
	if req.Quantity == hopperErrorQuantity {
		tx.State = "error"
		tx.ErrorCode = 1
		tx.ErrorType = "COIN_STUCK"
		f.fault, f.faultCode = "hopper_error", 1
		f.active = nil
		f.history[tx.ID] = tx
		f.writeJSON(w, 200, f.respond(tx))
		return
	}

	// The two resets of issue #3, keyed by quantity the way the Go mock keys
	// its scenarios: crashQuantity keeps the RTC count, powerLossQuantity does
	// not.  Both leave a transaction behind that a reboot can still be asked
	// about — that is what the terminal's 404 depends on.
	// One token falls after the motor stop, the way the disc coasts (issue #5).
	if req.Quantity == overrunQuantity {
		go f.runWithCoastToken(tx)
		f.writeJSON(w, 200, f.respond(tx))
		return
	}

	if req.Quantity == crashQuantity || req.Quantity == powerLossQuantity {
		go f.reboot(tx, req.Quantity == crashQuantity)
		f.writeJSON(w, 200, f.respond(tx))
		return
	}

	go f.run(tx)
	f.writeJSON(w, 200, f.respond(tx))
}

// reboot drops one token and then resets, the way a brownout on motor start
// does.  countSurvives says whether RTC memory came through it.
func (f *fakeDevice) reboot(tx *fakeTx, countSurvives bool) {
	time.Sleep(20 * time.Millisecond)
	f.mu.Lock()
	tx.Dispensed++
	f.mu.Unlock()

	time.Sleep(50 * time.Millisecond)
	f.mu.Lock()
	defer f.mu.Unlock()
	tx.State = "error"
	// A recovered crash is NOT a fault: nothing is wrong with the machine, and
	// a device that faults here takes itself out of service over a watchdog
	// reset (issue #6).
	tx.ErrorType = "RESET"
	if f.faultAfterReset {
		f.fault, f.faultCode = "jam", 0
	}
	if countSurvives {
		tx.Reliable = true
	} else {
		tx.Reliable = f.claimCountExact
		tx.Dispensed = 0 // all that is left is the lower bound from flash
	}
	f.active = nil
	if !f.forgetCrashedTx {
		f.history[tx.ID] = tx
	}
}

// runWithCoastToken dispenses the whole quantity and then, after the motor has
// been stopped, one more: the token that was already past the wheel.  A device
// that reports it is conforming; clampDispensed is the firmware before #5.
func (f *fakeDevice) runWithCoastToken(tx *fakeTx) {
	for i := 0; i < tx.Quantity; i++ {
		time.Sleep(5 * time.Millisecond)
		f.mu.Lock()
		tx.Dispensed++
		f.mu.Unlock()
	}
	time.Sleep(30 * time.Millisecond) // the settling window
	f.mu.Lock()
	if !f.clampDispensed {
		tx.Dispensed++
		f.overruns++
	}
	tx.State = "done"
	tx.Reliable = true
	f.history[tx.ID] = tx
	f.active = nil
	f.mu.Unlock()
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
	tx.Reliable = true
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
	body := map[string]any{
		"tx_id":     tx.ID,
		"state":     tx.State,
		"quantity":  tx.Quantity,
		"dispensed": tx.Dispensed,
	}
	if !f.omitCountReliable {
		body["count_reliable"] = tx.Reliable
	}
	if !f.omitErrorCode {
		body["error_code"] = tx.ErrorCode
		body["error_type"] = tx.ErrorType
	}
	return body
}

var _ = fmt.Sprintf
