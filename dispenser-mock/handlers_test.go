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
	if health.Status == "" || health.Firmware == "" || health.Dispenser == "" {
		t.Errorf("health is missing required fields: %+v", health)
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
// back, not 409 — the firmware does not do this yet (issue #2), which is
// precisely the divergence the conformance suite exists to surface.
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

func TestOtherTransactionWhileDispensingIs409(t *testing.T) {
	srv, _ := newTestServer(t)

	if status, _ := postDispense(t, srv, `{"tx_id":"tx-busy","quantity":15}`); status != http.StatusOK {
		t.Fatalf("first POST failed")
	}
	if status, body := postDispense(t, srv, `{"tx_id":"tx-other","quantity":1}`); status != http.StatusConflict {
		t.Errorf("second POST = %d, want 409 (body %s)", status, body)
	}
}

func TestDispenseWhileHardwareErrorIs409(t *testing.T) {
	srv, m := newTestServer(t)

	// Quantity 8 maps to the COIN_STUCK scenario.
	if status, _ := postDispense(t, srv, `{"tx_id":"tx-err","quantity":8}`); status != http.StatusOK {
		t.Fatalf("error scenario POST failed")
	}

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if e := m.GetHardwareError(); e != nil && e.Active {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if e := m.GetHardwareError(); e == nil || !e.Active {
		t.Fatalf("scenario did not produce an active hardware error")
	}

	if status, body := postDispense(t, srv, `{"tx_id":"tx-after","quantity":1}`); status != http.StatusConflict {
		t.Errorf("POST during an active hardware error = %d, want 409 (body %s)", status, body)
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
