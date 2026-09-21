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

// fetchNonce asks the mock for a nonce, the way every client must since #8.
func fetchNonce(t *testing.T, srv *httptest.Server) string {
	t.Helper()
	status, body := do(t, srv, http.MethodGet, "/nonce", "", nil)
	if status != http.StatusOK {
		t.Fatalf("GET /nonce = %d", status)
	}
	var n NonceResponse
	if err := json.Unmarshal([]byte(body), &n); err != nil {
		t.Fatalf("/nonce is not JSON: %v", err)
	}
	if len(n.Nonce) != NonceHexLen {
		t.Fatalf("/nonce handed out %q, want %d hex characters", n.Nonce, NonceHexLen)
	}
	return n.Nonce
}

// doSigned signs the request the way the protocol requires: a fresh nonce,
// then HMAC-SHA256 over METHOD \n PATH \n BODY \n NONCE.  There is no
// X-API-Key anywhere in this file any more, and that is the point of #8.
func doSigned(t *testing.T, srv *httptest.Server, method, path, body string, headers map[string]string) (int, string) {
	t.Helper()
	nonce := fetchNonce(t, srv)
	h := map[string]string{}
	for k, v := range headers {
		h[k] = v
	}
	h["X-Nonce"] = nonce
	h["X-Signature"] = SignRequest("test-key", method, path, body, nonce)
	return do(t, srv, method, path, body, h)
}

func postDispense(t *testing.T, srv *httptest.Server, body string) (int, string) {
	t.Helper()
	return doSigned(t, srv, http.MethodPost, "/dispense", body, map[string]string{
		"Content-Type": "application/json",
	})
}

// The mock must report the protocol version the client insists on;
// a mock one version behind is a mock that tests the wrong contract.
func TestHealthReportsProtocolVersion(t *testing.T) {
	srv, _ := newTestServer(t)

	// Signed, because `firmware` below is on the authenticated side of the
	// document since #8.  That the VERSION itself is readable without a
	// signature — it has to be, or no client could complete the handshake
	// before it signs — is TestHealthIsMinimalWithoutASignature's business.
	status, body := doSigned(t, srv, http.MethodGet, "/health", "", nil)
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
	status, body := doSigned(t, srv, http.MethodGet, "/debug", "", nil)
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
	status, _ := doSigned(t, srv, http.MethodPost, "/reset", "",
		map[string]string{"Content-Type": "application/json"})
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

	// The nonce these cases mis-sign with is a real one: an unknown nonce and
	// a wrong signature must be two different 401s, and only a live nonce can
	// produce the second.
	live := fetchNonce(t, srv)
	const body = `{"tx_id":"a","quantity":1}`

	cases := []struct {
		name       string
		method     string
		path       string
		body       string
		headers    map[string]string
		wantReason string
	}{
		{"post with no signature at all", http.MethodPost, "/dispense", body,
			map[string]string{"Content-Type": "application/json"}, "signature"},
		{"post carrying the old X-API-Key", http.MethodPost, "/dispense", body,
			// The header of protocol 2.  It is not a credential any more and
			// not a fallback either: this request is simply unsigned.
			map[string]string{"Content-Type": "application/json", "X-API-Key": "test-key"}, "signature"},
		{"post signed with the wrong key", http.MethodPost, "/dispense", body,
			map[string]string{"Content-Type": "application/json", "X-Nonce": live,
				"X-Signature": SignRequest("not-the-key", http.MethodPost, "/dispense", body, live)}, "signature"},
		{"post with a nonce nobody issued", http.MethodPost, "/dispense", body,
			map[string]string{"Content-Type": "application/json",
				"X-Nonce":     strings.Repeat("f", NonceHexLen),
				"X-Signature": SignRequest("test-key", http.MethodPost, "/dispense", body, strings.Repeat("f", NonceHexLen))}, "nonce"},
		{"status without a signature", http.MethodGet, "/dispense/a", "", nil, "signature"},
		{"debug without a signature", http.MethodGet, "/debug", "", nil, "signature"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			status, got := do(t, srv, tc.method, tc.path, tc.body, tc.headers)
			if status != http.StatusUnauthorized {
				t.Fatalf("status = %d, want 401 (body %s)", status, got)
			}
			var resp ErrorResponse
			if err := json.Unmarshal([]byte(got), &resp); err != nil {
				t.Fatalf("401 body is not JSON: %v", err)
			}
			// The reason is the whole of the client contract: "nonce" means
			// fetch a fresh one and retry once, "signature" means stop.
			if resp.Reason != tc.wantReason {
				t.Errorf("401 reason = %q, want %q", resp.Reason, tc.wantReason)
			}
		})
	}
}

// The replay this issue exists to stop: the identical signed POST, twice.
func TestReplayedSignedPostIs401(t *testing.T) {
	srv, _ := newTestServer(t)
	nonce := fetchNonce(t, srv)
	body := `{"tx_id":"replay01","quantity":1}`
	headers := map[string]string{
		"Content-Type": "application/json",
		"X-Nonce":      nonce,
		"X-Signature":  SignRequest("test-key", http.MethodPost, "/dispense", body, nonce),
	}

	if status, got := do(t, srv, http.MethodPost, "/dispense", body, headers); status != http.StatusOK {
		t.Fatalf("first signed POST = %d, want 200 (body %s)", status, got)
	}
	status, got := do(t, srv, http.MethodPost, "/dispense", body, headers)
	if status != http.StatusUnauthorized {
		t.Fatalf("replayed POST = %d, want 401 (body %s)", status, got)
	}
	var resp ErrorResponse
	_ = json.Unmarshal([]byte(got), &resp)
	if resp.Reason != "nonce" {
		t.Errorf("replay reason = %q, want %q", resp.Reason, "nonce")
	}
}

// A body changed after signing — a captured request re-aimed at more tokens.
func TestTamperedBodyIs401(t *testing.T) {
	srv, _ := newTestServer(t)
	nonce := fetchNonce(t, srv)
	signedBody := `{"tx_id":"tamper01","quantity":1}`
	sentBody := `{"tx_id":"tamper01","quantity":9}`

	status, got := do(t, srv, http.MethodPost, "/dispense", sentBody, map[string]string{
		"Content-Type": "application/json",
		"X-Nonce":      nonce,
		"X-Signature":  SignRequest("test-key", http.MethodPost, "/dispense", signedBody, nonce),
	})
	if status != http.StatusUnauthorized {
		t.Fatalf("tampered body = %d, want 401 (body %s)", status, got)
	}
}

// A POST spends its nonce for MUTATING use only: the status polls behind it
// keep working off the same one, which is what "one GET /nonce per
// transaction" means.
func TestASpentNonceStillPolls(t *testing.T) {
	srv, _ := newTestServer(t)
	nonce := fetchNonce(t, srv)
	body := `{"tx_id":"spent001","quantity":1}`

	if status, got := do(t, srv, http.MethodPost, "/dispense", body, map[string]string{
		"Content-Type": "application/json",
		"X-Nonce":      nonce,
		"X-Signature":  SignRequest("test-key", http.MethodPost, "/dispense", body, nonce),
	}); status != http.StatusOK {
		t.Fatalf("signed POST = %d (body %s)", status, got)
	}

	for i := 0; i < 3; i++ {
		status, got := do(t, srv, http.MethodGet, "/dispense/spent001", "", map[string]string{
			"X-Nonce":     nonce,
			"X-Signature": SignRequest("test-key", http.MethodGet, "/dispense/spent001", "", nonce),
		})
		if status != http.StatusOK {
			t.Fatalf("poll %d on the spent nonce = %d, want 200 (body %s)", i, status, got)
		}
	}
}

// GET /health answers TWO documents off one URL (issue #8).
func TestHealthIsMinimalWithoutASignature(t *testing.T) {
	srv, _ := newTestServer(t)

	status, body := do(t, srv, http.MethodGet, "/health", "", nil)
	if status != http.StatusOK {
		t.Fatalf("unsigned GET /health = %d, want 200 — it is a liveness probe, not a protected route", status)
	}
	var minimal MinimalHealthResponse
	if err := json.Unmarshal([]byte(body), &minimal); err != nil {
		t.Fatalf("health is not JSON: %v", err)
	}
	if minimal.Protocol != ProtocolVersion || minimal.State == "" || minimal.Fault == "" {
		t.Errorf("the unsigned document is missing one of its three fields: %s", body)
	}
	if minimal.Authenticated {
		t.Errorf("the unsigned document claims to be authenticated: %s", body)
	}
	// Everything that needs the key must be absent, not zeroed.
	for _, field := range []string{"wifi", "ssid", "metrics", "heap_free", "reset_reason",
		"firmware", "uptime", "error_history", "fault_code"} {
		if strings.Contains(body, `"`+field+`"`) {
			t.Errorf("the unsigned /health leaks %q: %s", field, body)
		}
	}

	status, body = doSigned(t, srv, http.MethodGet, "/health", "", nil)
	if status != http.StatusOK {
		t.Fatalf("signed GET /health = %d", status)
	}
	var full HealthResponse
	if err := json.Unmarshal([]byte(body), &full); err != nil {
		t.Fatalf("signed health is not JSON: %v", err)
	}
	if !full.Authenticated {
		t.Errorf("the signed document does not say so: %s", body)
	}
	if full.WiFi == nil || full.Firmware == "" || full.ResetReason == "" {
		t.Errorf("the signed document is missing fields: %s", body)
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
	status, body := doSigned(t, srv, http.MethodPost, "/dispense", `{"tx_id":"ct","quantity":1}`,
		map[string]string{"Content-Type": "text/plain"})
	if status != http.StatusUnsupportedMediaType {
		t.Errorf("status = %d, want 415 (body %s)", status, body)
	}
}

func TestUnknownTransactionIs404(t *testing.T) {
	srv, _ := newTestServer(t)
	status, _ := doSigned(t, srv, http.MethodGet, "/dispense/nosuchtx", "", nil)
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
		status, body := doSigned(t, srv, http.MethodGet, "/dispense/"+txID, "", nil)
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
	if status, body := doSigned(t, srv, http.MethodGet, "/dispense/tx-err", "", nil); status != http.StatusOK {
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
		_, body := doSigned(t, srv, http.MethodGet, "/dispense/tx-why", "", nil)
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
	status, body := doSigned(t, srv, "POST", "/dispense", "",
		map[string]string{"Content-Type": "application/json"})
	if status != http.StatusBadRequest {
		t.Fatalf("empty body: status %d, want 400 (body: %s)", status, body)
	}
}

func TestPostWithOversizedBodyIs413(t *testing.T) {
	srv, _ := newTestServer(t)
	padding := strings.Repeat("x", MaxRequestBody)
	body := `{"tx_id":"big1","quantity":1,"pad":"` + padding + `"}`
	status, got := doSigned(t, srv, "POST", "/dispense", body,
		map[string]string{"Content-Type": "application/json"})
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
	status, got := doSigned(t, srv, "POST", "/dispense", body,
		map[string]string{"Content-Type": "application/json"})
	if status != http.StatusOK {
		t.Fatalf("body of exactly the cap: status %d, want 200 (body: %s)", status, got)
	}
}
