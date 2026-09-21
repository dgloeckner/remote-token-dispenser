package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// newTestServer starts the mock's own handlers on a throwaway HTTP server.
func newTestServer(t *testing.T) (*httptest.Server, *MockDispenser) {
	t.Helper()
	m := NewMockDispenser("test-key")
	mux := http.NewServeMux()
	m.RegisterHandlers(mux)
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)
	return srv, m
}

func do(t *testing.T, srv *httptest.Server, method, path, body string, headers map[string]string) (int, string) {
	t.Helper()
	var rdr *strings.Reader
	if body != "" {
		rdr = strings.NewReader(body)
	}
	var req *http.Request
	var err error
	if rdr == nil {
		req, err = http.NewRequest(method, srv.URL+path, nil)
	} else {
		req, err = http.NewRequest(method, srv.URL+path, rdr)
	}
	if err != nil {
		t.Fatalf("request: %v", err)
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := srv.Client().Do(req)
	if err != nil {
		t.Fatalf("do: %v", err)
	}
	defer resp.Body.Close()
	blob, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("read body: %v", err)
	}
	return resp.StatusCode, string(blob)
}

func postDispense(t *testing.T, srv *httptest.Server, body string) (int, string) {
	t.Helper()
	return do(t, srv, http.MethodPost, "/dispense", body, map[string]string{
		"Content-Type": "application/json",
		"X-API-Key":    "test-key",
	})
}

// The mock must report the protocol version the client insists on;
// a mock one version behind is a mock that tests the wrong contract.
func TestHealthReportsProtocolVersion(t *testing.T) {
	srv, _ := newTestServer(t)

	status, body := do(t, srv, http.MethodGet, "/health", "", nil)
	if status != http.StatusOK {
		t.Fatalf("GET /health = %d, want 200", status)
	}

	var health HealthResponse
	if err := json.Unmarshal([]byte(body), &health); err != nil {
		t.Fatalf("health is not JSON: %v", err)
	}
	if health.Protocol != ProtocolVersion {
		t.Errorf("health.protocol = %d, want %d", health.Protocol, ProtocolVersion)
	}
	if health.Firmware == "" || health.State == "" || health.Fault == "" {
		t.Errorf("health is missing required fields: %+v", health)
	}
	if health.State != StateIdle {
		t.Errorf("health.state = %q on a fresh mock, want %q", health.State, StateIdle)
	}
	if health.Fault != FaultNone {
		t.Errorf("health.fault = %q on a fresh mock, want %q", health.Fault, FaultNone)
	}
}

// --protocol changes what the mock CLAIMS and nothing else, so a terminal can
// be pointed at a device it has to refuse (dgloeckner/clubbar#948).
func TestProtocolFlagChangesOnlyTheClaim(t *testing.T) {
	m := NewMockDispenserWithProtocol("test-key", 1)
	mux := http.NewServeMux()
	m.RegisterHandlers(mux)
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)

	status, body := do(t, srv, http.MethodGet, "/health", "", nil)
	if status != http.StatusOK {
		t.Fatalf("GET /health = %d, want 200", status)
	}
	var health HealthResponse
	if err := json.Unmarshal([]byte(body), &health); err != nil {
		t.Fatalf("health is not JSON: %v", err)
	}
	if health.Protocol != 1 {
		t.Errorf("health.protocol = %d, want the claimed 1", health.Protocol)
	}
	if health.State != StateIdle || health.Fault != FaultNone {
		t.Errorf("the flag changed behaviour, not just the claim: %+v", health)
	}
}

func TestHealthHasNoHopperLow(t *testing.T) {
	srv, _ := newTestServer(t)
	_, body := do(t, srv, http.MethodGet, "/health", "", nil)
	if strings.Contains(body, "hopper_low") {
		t.Errorf("/health still publishes the empty sensor this hopper does not have: %s", body)
	}
}

func TestDebugNeedsTheKeyAndCarriesThePins(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := do(t, srv, http.MethodGet, "/debug", "", nil); status != http.StatusUnauthorized {
		t.Errorf("GET /debug without a key = %d, want 401", status)
	}
	status, body := do(t, srv, http.MethodGet, "/debug", "",
		map[string]string{"X-API-Key": "test-key"})
	if status != http.StatusOK {
		t.Fatalf("GET /debug = %d, want 200", status)
	}
	if !strings.Contains(body, "coin_pulse") || !strings.Contains(body, "error_signal") {
		t.Errorf("/debug carries no pin levels: %s", body)
	}
	if strings.Contains(body, "hopper_low") {
		t.Errorf("/debug publishes hopper_low: %s", body)
	}
}

// A fault is ended by a power cycle and by nothing else (owner decision,
// 2026-09-20).  A reset route would be a second way out, and the terminal
// would grow a button for it.
func TestNoResetRoute(t *testing.T) {
	srv, _ := newTestServer(t)
	status, _ := do(t, srv, http.MethodPost, "/reset", "",
		map[string]string{"X-API-Key": "test-key", "Content-Type": "application/json"})
	if status == http.StatusOK || status == http.StatusNoContent {
		t.Errorf("POST /reset = %d: the device has a way out of a fault that is not a power cycle", status)
	}
}

func TestHealthNeedsNoAPIKey(t *testing.T) {
	srv, _ := newTestServer(t)
	if status, _ := do(t, srv, http.MethodGet, "/health", "", nil); status != http.StatusOK {
		t.Errorf("GET /health without key = %d, want 200", status)
	}
}

func TestAuthentication(t *testing.T) {
	srv, _ := newTestServer(t)

	cases := []struct {
		name    string
		method  string
		path    string
		body    string
		headers map[string]string
	}{
		{"post without key", http.MethodPost, "/dispense", `{"tx_id":"a","quantity":1}`,
			map[string]string{"Content-Type": "application/json"}},
		{"post with wrong key", http.MethodPost, "/dispense", `{"tx_id":"a","quantity":1}`,
			map[string]string{"Content-Type": "application/json", "X-API-Key": "nope"}},
		{"status without key", http.MethodGet, "/dispense/a", "", nil},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if status, body := do(t, srv, tc.method, tc.path, tc.body, tc.headers); status != http.StatusUnauthorized {
				t.Errorf("status = %d, want 401 (body %s)", status, body)
			}
		})
	}
}

func TestDispenseValidation(t *testing.T) {
	srv, _ := newTestServer(t)

	cases := []struct {
		name string
		body string
		want int
	}{
		{"invalid json", `{"tx_id":`, http.StatusBadRequest},
		{"empty tx_id", `{"tx_id":"","quantity":1}`, http.StatusBadRequest},
		{"overlong tx_id", `{"tx_id":"0123456789abcdefg","quantity":1}`, http.StatusBadRequest},
		{"quantity zero", `{"tx_id":"q0","quantity":0}`, http.StatusBadRequest},
		{"quantity above max", `{"tx_id":"q21","quantity":21}`, http.StatusBadRequest},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if status, body := postDispense(t, srv, tc.body); status != tc.want {
				t.Errorf("status = %d, want %d (body %s)", status, tc.want, body)
			}
		})
	}
}

func TestDispenseRequiresJSONContentType(t *testing.T) {
	srv, _ := newTestServer(t)
	status, body := do(t, srv, http.MethodPost, "/dispense", `{"tx_id":"ct","quantity":1}`,
		map[string]string{"Content-Type": "text/plain", "X-API-Key": "test-key"})
	if status != http.StatusUnsupportedMediaType {
		t.Errorf("status = %d, want 415 (body %s)", status, body)
	}
}

func TestUnknownTransactionIs404(t *testing.T) {
	srv, _ := newTestServer(t)
	status, _ := do(t, srv, http.MethodGet, "/dispense/nosuchtx", "",
		map[string]string{"X-API-Key": "test-key"})
	if status != http.StatusNotFound {
		t.Errorf("status = %d, want 404", status)
	}
}

// waitForState polls until the transaction leaves "dispensing".
func waitForFinal(t *testing.T, srv *httptest.Server, txID string) DispenseResponse {
	t.Helper()
	deadline := time.Now().Add(10 * time.Second)
	var last DispenseResponse
	for time.Now().Before(deadline) {
		status, body := do(t, srv, http.MethodGet, "/dispense/"+txID, "",
			map[string]string{"X-API-Key": "test-key"})
		if status != http.StatusOK {
			t.Fatalf("GET /dispense/%s = %d", txID, status)
		}
		if err := json.Unmarshal([]byte(body), &last); err != nil {
			t.Fatalf("status body is not JSON: %v", err)
		}
		if last.State != StateDispensing {
			return last
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Fatalf("transaction %s never left dispensing", txID)
	return last
}

func TestDispenseReachesDoneAndReplayIsIdempotent(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, body := postDispense(t, srv, `{"tx_id":"tx-done","quantity":1}`); status != http.StatusOK {
		t.Fatalf("POST = %d, want 200 (body %s)", status, body)
	}

	final := waitForFinal(t, srv, "tx-done")
	if final.State != StateDone || final.Dispensed != 1 {
		t.Fatalf("final = %+v, want done/1", final)
	}

	status, body := postDispense(t, srv, `{"tx_id":"tx-done","quantity":1}`)
	if status != http.StatusOK {
		t.Fatalf("replay = %d, want 200", status)
	}
	var replay DispenseResponse
	if err := json.Unmarshal([]byte(body), &replay); err != nil {
		t.Fatalf("replay body is not JSON: %v", err)
	}
	if replay.State != StateDone || replay.Dispensed != 1 {
		t.Errorf("replay = %+v, want the cached done/1", replay)
	}
}

// The protocol promises that a retry of the ACTIVE transaction gets its state
// back, not 409. The firmware does this too since #2; the mock has always
// implemented the promise, which is why the divergence only showed up once the
// conformance suite ran against both.
func TestRetryOfActiveTransactionIs200(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := postDispense(t, srv, `{"tx_id":"tx-slow","quantity":15}`); status != http.StatusOK {
		t.Fatalf("first POST failed")
	}
	status, body := postDispense(t, srv, `{"tx_id":"tx-slow","quantity":15}`)
	if status != http.StatusOK {
		t.Fatalf("retry of the active tx = %d, want 200 (body %s)", status, body)
	}
	if !strings.Contains(body, "tx-slow") {
		t.Errorf("retry body does not name the transaction: %s", body)
	}
}

// Same id, other quantity: not a retry but a client that contradicts itself.
// Answering with the cached quantity would look like the confirmation of a
// request nobody made (issue #2).
func TestSameTxIDWithDifferentQuantityIs409(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := postDispense(t, srv, `{"tx_id":"tx-reuse","quantity":15}`); status != http.StatusOK {
		t.Fatalf("first POST failed")
	}

	status, body := postDispense(t, srv, `{"tx_id":"tx-reuse","quantity":2}`)
	if status != http.StatusConflict {
		t.Fatalf("reused id with another quantity = %d, want 409 (body %s)", status, body)
	}
	var errResp ErrorResponse
	if err := json.Unmarshal([]byte(body), &errResp); err != nil {
		t.Fatalf("body is not JSON: %v", err)
	}
	if errResp.Error != "tx_id reused" {
		t.Errorf("error = %q, want %q", errResp.Error, "tx_id reused")
	}
}

func TestOtherTransactionWhileDispensingIs409(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := postDispense(t, srv, `{"tx_id":"tx-busy","quantity":15}`); status != http.StatusOK {
		t.Fatalf("first POST failed")
	}
	if status, body := postDispense(t, srv, `{"tx_id":"tx-other","quantity":1}`); status != http.StatusConflict {
		t.Errorf("second POST = %d, want 409 (body %s)", status, body)
	}
}

func TestDispenseWhileFaultedIs409(t *testing.T) {
	srv, m := newTestServer(t)

	// Quantity 8 maps to the COIN_STUCK scenario, which faults the device.
	if status, _ := postDispense(t, srv, `{"tx_id":"tx-err","quantity":8}`); status != http.StatusOK {
		t.Fatalf("error scenario POST failed")
	}

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if fault, _ := m.GetFault(); fault != FaultNone {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	fault, code := m.GetFault()
	if fault != FaultHopperError {
		t.Fatalf("scenario left fault %q, want %q", fault, FaultHopperError)
	}
	if code != 1 {
		t.Errorf("fault code = %d, want the COIN_STUCK 1", code)
	}

	status, body := postDispense(t, srv, `{"tx_id":"tx-after","quantity":1}`)
	if status != http.StatusConflict {
		t.Fatalf("POST while faulted = %d, want 409 (body %s)", status, body)
	}
	if !strings.Contains(body, `"fault"`) {
		t.Errorf("the 409 does not name the fault: %s", body)
	}

	// The retry of the transaction that failed is still answered: the
	// terminal is asking what happened, not asking for tokens.
	if status, body := postDispense(t, srv, `{"tx_id":"tx-err","quantity":8}`); status != http.StatusOK {
		t.Errorf("idempotent retry while faulted = %d, want 200 (body %s)", status, body)
	}
	if status, body := do(t, srv, http.MethodGet, "/dispense/tx-err", "",
		map[string]string{"X-API-Key": "test-key"}); status != http.StatusOK {
		t.Errorf("GET of the failed transaction while faulted = %d, want 200 (body %s)", status, body)
	}
}

// A failed transaction says WHY, so the terminal can tell a jam from a motor
// fault from an empty hopper (issue #6).
func TestFailedTransactionCarriesItsErrorType(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := postDispense(t, srv, `{"tx_id":"tx-why","quantity":8}`); status != http.StatusOK {
		t.Fatalf("error scenario POST failed")
	}
	deadline := time.Now().Add(2 * time.Second)
	var resp DispenseResponse
	for time.Now().Before(deadline) {
		_, body := do(t, srv, http.MethodGet, "/dispense/tx-why", "",
			map[string]string{"X-API-Key": "test-key"})
		if err := json.Unmarshal([]byte(body), &resp); err != nil {
			t.Fatalf("status is not JSON: %v", err)
		}
		if resp.State == StateError {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if resp.State != StateError {
		t.Fatalf("transaction state = %q, want error", resp.State)
	}
	if resp.ErrorCode != 1 || resp.ErrorType != "COIN_STUCK" {
		t.Errorf("error_code/error_type = %d/%q, want 1/COIN_STUCK", resp.ErrorCode, resp.ErrorType)
	}
}

// …and a transaction that went through says so with the same pair, rather
// than leaving the fields out for a reader to default.
func TestSuccessfulTransactionCarriesErrorTypeNone(t *testing.T) {
	srv, _ := newTestServer(t)

	_, body := postDispense(t, srv, `{"tx_id":"tx-fine","quantity":1}`)
	if !strings.Contains(body, `"error_type"`) || !strings.Contains(body, `"error_code"`) {
		t.Fatalf("the POST answer carries no error_code/error_type: %s", body)
	}
	var resp DispenseResponse
	if err := json.Unmarshal([]byte(body), &resp); err != nil {
		t.Fatalf("response is not JSON: %v", err)
	}
	if resp.ErrorCode != 0 || resp.ErrorType != TxErrorNone {
		t.Errorf("error_code/error_type = %d/%q, want 0/%s", resp.ErrorCode, resp.ErrorType, TxErrorNone)
	}
}

func TestScenarioMapping(t *testing.T) {
	// The scenario table is the mock's test surface; a silent change to it
	// silently changes what every conformance run exercises.
	want := map[int]string{
		1: "success", 4: "timeout_partial", 5: "crash_after_first", 6: "partial_dispense",
		7: "load_delay", 8: "error_coin_stuck", 15: "slow_dispense", 20: "success", 21: "invalid",
	}
	for qty, scenario := range want {
		if got := GetScenarioForQuantity(qty); got != scenario {
			t.Errorf("quantity %d maps to %q, want %q", qty, got, scenario)
		}
	}
}

// --- issue #4: the request body ---------------------------------------------

func TestPostWithoutBodyIs400(t *testing.T) {
	srv, _ := newTestServer(t)
	status, body := do(t, srv, "POST", "/dispense", "",
		map[string]string{"Content-Type": "application/json", "X-API-Key": "test-key"})
	if status != http.StatusBadRequest {
		t.Fatalf("empty body: status %d, want 400 (body: %s)", status, body)
	}
}

func TestPostWithOversizedBodyIs413(t *testing.T) {
	srv, _ := newTestServer(t)
	padding := strings.Repeat("x", MaxRequestBody)
	body := `{"tx_id":"big1","quantity":1,"pad":"` + padding + `"}`
	status, got := do(t, srv, "POST", "/dispense", body,
		map[string]string{"Content-Type": "application/json", "X-API-Key": "test-key"})
	if status != http.StatusRequestEntityTooLarge {
		t.Fatalf("oversized body: status %d, want 413 (body: %s)", status, got)
	}
}

func TestPostAtTheBodyCapIsStillAccepted(t *testing.T) {
	srv, _ := newTestServer(t)
	body := `{"tx_id":"cap1","quantity":1,"pad":"`
	body += strings.Repeat("x", MaxRequestBody-len(body)-2) + `"}`
	if len(body) != MaxRequestBody {
		t.Fatalf("test built a %d byte body, wanted exactly %d", len(body), MaxRequestBody)
	}
	status, got := do(t, srv, "POST", "/dispense", body,
		map[string]string{"Content-Type": "application/json", "X-API-Key": "test-key"})
	if status != http.StatusOK {
		t.Fatalf("body of exactly the cap: status %d, want 200 (body: %s)", status, got)
	}
}
