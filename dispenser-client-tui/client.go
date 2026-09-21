package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// ProtocolVersion is the only protocol version this client speaks.
// The handshake is deliberately strict: there are no devices in the field, so
// a mismatch is a bug to fix, never something to adapt to at runtime.
const ProtocolVersion = 2

// HealthResponse matches GET /health from the dispenser protocol.
//
// Since issue #6 there is ONE state and ONE fault.  The old document carried
// `status` (ok|degraded|error) next to `dispenser` (idle|dispensing|error),
// and this client ORed the two together — which meant nothing decided what
// the terminal showed when they disagreed.
type HealthResponse struct {
	Protocol int    `json:"protocol"`
	State    string `json:"state"`
	// Fault is the device-level condition: none | jam | hopper_error.  Only a
	// power cycle clears it (owner decision, 2026-09-20).
	Fault string `json:"fault"`
	// FaultCode is the Azkoyen code behind a hopper_error, 0 otherwise.  A
	// POINTER, like CountReliable and OverrunTokens: a device that does not
	// send the field must stay distinguishable from one that sends 0.
	FaultCode *int   `json:"fault_code"`
	Uptime    int    `json:"uptime"`
	Firmware  string `json:"firmware"`
	// HeapFree is the free heap in bytes (issue #7).  A POINTER: a device
	// that does not report it must stay distinguishable from one reporting 0,
	// which would read as "out of memory" on every panel that shows it.
	HeapFree *int `json:"heap_free"`
	// ResetReason names the last reset ("Power on", "External System",
	// "Software Watchdog", …).  A device that resets once a week left no
	// trace at all before this: uptime was simply small again.
	ResetReason  string        `json:"reset_reason"`
	WiFi         *WiFiInfo     `json:"wifi,omitempty"`
	Metrics      Metrics       `json:"metrics"`
	ActiveTx     *ActiveTxInfo `json:"active_tx,omitempty"`
	ErrorHistory []ErrorRecord `json:"error_history,omitempty"`
}

// DebugResponse matches GET /debug: the raw pin levels, which moved out of
// /health in issue #6.  They are a bench instrument, not something a monitor
// should act on, and the authenticated health document is not the place for a
// reading nobody can interpret without the wiring diagram.
type DebugResponse struct {
	GPIO *GPIOInfo `json:"gpio"`
}

type Metrics struct {
	TotalDispenses int `json:"total_dispenses"`
	Successful     int `json:"successful"`
	Jams           int `json:"jams"`
	Partial        int `json:"partial"`
	Failures       int `json:"failures"`
	// OverrunTokens counts the tokens that left the hopper past the requested
	// quantity (issue #5).  A POINTER, like CountReliable: a device that does
	// not report the metric must be distinguishable from one that reports
	// zero, or "we never looked" reads as "it never happened".
	OverrunTokens *int   `json:"overrun_tokens"`
	LastError     string `json:"last_error"`
	LastErrorType string `json:"last_error_type"`
}

type ActiveTxInfo struct {
	TxID      string `json:"tx_id"`
	Quantity  int    `json:"quantity"`
	Dispensed int    `json:"dispensed"`
}

type WiFiInfo struct {
	RSSI int    `json:"rssi"`
	IP   string `json:"ip"`
	SSID string `json:"ssid"`
	// Reconnects counts the times the link came back since boot (issue #7).
	// A POINTER for the same reason as the rest: zero reconnects is the good
	// news, "the device does not count them" is not news at all.
	Reconnects *int `json:"reconnects"`
}

// GPIOInfo is what GET /debug reports.  There is no hopper_low any more: the
// empty sensor is a factory option this hopper does not have, so the pin sat
// on its pull-up and reported "not empty" forever (issue #6).
type GPIOInfo struct {
	CoinPulse struct {
		Raw    int  `json:"raw"`
		Active bool `json:"active"`
	} `json:"coin_pulse"`
	ErrorSignal struct {
		Raw    int  `json:"raw"`
		Active bool `json:"active"`
	} `json:"error_signal"`
}

// ErrorRecord is one decoded hopper error.  It has no `cleared` flag any
// more: an error raises a fault, and a fault outlives every transaction until
// somebody pulls the plug — so nothing could clear it.
type ErrorRecord struct {
	Code      int    `json:"code"`
	Type      string `json:"type"`
	Timestamp int64  `json:"timestamp"`
}

// DispenseRequest matches POST /dispense
type DispenseRequest struct {
	TxID     string `json:"tx_id"`
	Quantity int    `json:"quantity"`
}

// DispenseResponse matches dispense endpoint responses
type DispenseResponse struct {
	TxID      string `json:"tx_id"`
	State     string `json:"state"`
	Quantity  int    `json:"quantity"`
	Dispensed int    `json:"dispensed"`
	// CountReliable is required on every transaction response: false means
	// Dispensed is a lower bound, because the device lost power mid-dispense
	// and the live count went with it.  A POINTER on purpose — a missing field
	// must be distinguishable from an explicit false, or a device that does not
	// implement it reads as "the count is unreliable" everywhere.
	CountReliable *bool `json:"count_reliable"`
	// ErrorCode / ErrorType say WHY a transaction ended in "error" (issue #6):
	// the Azkoyen code 1-7 with its name, or 0 with JAM_TIMEOUT or RESET.
	// Required on every transaction response, so ErrorCode is a pointer: 0 and
	// "the device never told us" are different answers.
	ErrorCode *int   `json:"error_code"`
	ErrorType string `json:"error_type"`
	Error     string `json:"error,omitempty"`
}

// ErrorResponse for 4xx/5xx.  A 409 names WHICH 409 it is: `busy` with the
// blocking transaction, `tx_id reused`, or `fault` with the fault and its
// Azkoyen code (issue #6).
type ErrorResponse struct {
	Error      string `json:"error"`
	ActiveTxID string `json:"active_tx_id,omitempty"`
	Fault      string `json:"fault,omitempty"`
	FaultCode  int    `json:"fault_code,omitempty"`
}

// DispenserClient wraps HTTP calls to the ESP8266
type DispenserClient struct {
	BaseURL    string
	APIKey     string
	HTTPClient *http.Client
}

func NewDispenserClient(baseURL, apiKey string, timeout time.Duration) *DispenserClient {
	// Normalize base URL
	baseURL = strings.TrimRight(baseURL, "/")
	if !strings.HasPrefix(baseURL, "http") {
		baseURL = "http://" + baseURL
	}

	return &DispenserClient{
		BaseURL: baseURL,
		APIKey:  apiKey,
		HTTPClient: &http.Client{
			Timeout: timeout,
		},
	}
}

type APIResult struct {
	StatusCode int
	Latency    time.Duration
	Error      error
}

// Health fetches GET /health (no auth required)
func (c *DispenserClient) Health() (*HealthResponse, APIResult) {
	start := time.Now()

	req, err := http.NewRequest("GET", c.BaseURL+"/health", nil)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}

	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	defer resp.Body.Close()

	latency := time.Since(start)
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	if resp.StatusCode != 200 {
		return nil, APIResult{
			StatusCode: resp.StatusCode,
			Error:      fmt.Errorf("health returned %d: %s", resp.StatusCode, string(body)),
			Latency:    latency,
		}
	}

	var health HealthResponse
	if err := json.Unmarshal(body, &health); err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	if err := CheckProtocol(health.Protocol); err != nil {
		return &health, APIResult{StatusCode: 200, Error: err, Latency: latency}
	}

	return &health, APIResult{StatusCode: 200, Latency: latency}
}

// Debug fetches GET /debug (auth required): the raw pin levels for the bench.
func (c *DispenserClient) Debug() (*DebugResponse, APIResult) {
	start := time.Now()

	req, err := http.NewRequest("GET", c.BaseURL+"/debug", nil)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	req.Header.Set("X-API-Key", c.APIKey)

	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	defer resp.Body.Close()

	latency := time.Since(start)
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}
	if resp.StatusCode != 200 {
		return nil, APIResult{
			StatusCode: resp.StatusCode,
			Error:      fmt.Errorf("debug returned %d: %s", resp.StatusCode, string(body)),
			Latency:    latency,
		}
	}

	var debug DebugResponse
	if err := json.Unmarshal(body, &debug); err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}
	return &debug, APIResult{StatusCode: 200, Latency: latency}
}

// Dispense sends POST /dispense (auth required)
func (c *DispenserClient) Dispense(txID string, quantity int) (*DispenseResponse, APIResult) {
	start := time.Now()

	payload, _ := json.Marshal(DispenseRequest{TxID: txID, Quantity: quantity})
	req, err := http.NewRequest("POST", c.BaseURL+"/dispense", strings.NewReader(string(payload)))
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-API-Key", c.APIKey)

	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	defer resp.Body.Close()

	latency := time.Since(start)
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	result := APIResult{StatusCode: resp.StatusCode, Latency: latency}

	if resp.StatusCode == 409 {
		// A 409 is TWO different answers since issue #6 — "another transaction
		// is running" and "the device is faulted" — and this read them both as
		// busy, so a jammed machine was reported as an occupied one, with an
		// empty tx_id where the name of the blocking transaction belongs.
		var errResp ErrorResponse
		json.Unmarshal(body, &errResp)
		msg := fmt.Sprintf("busy: active tx %s", errResp.ActiveTxID)
		if errResp.Error == "fault" {
			msg = fmt.Sprintf("fault: %s", errResp.Fault)
			if errResp.FaultCode != 0 {
				msg = fmt.Sprintf("fault: %s (code %d)", errResp.Fault, errResp.FaultCode)
			}
		} else if errResp.Error == "tx_id reused" {
			msg = "tx_id reused with another quantity"
		}
		return nil, APIResult{
			StatusCode: 409,
			Error:      fmt.Errorf("%s", msg),
			Latency:    latency,
		}
	}

	if resp.StatusCode == 401 {
		return nil, APIResult{StatusCode: 401, Error: fmt.Errorf("unauthorized"), Latency: latency}
	}

	if resp.StatusCode != 200 {
		return nil, APIResult{
			StatusCode: resp.StatusCode,
			Error:      fmt.Errorf("dispense returned %d: %s", resp.StatusCode, string(body)),
			Latency:    latency,
		}
	}

	var dispResp DispenseResponse
	if err := json.Unmarshal(body, &dispResp); err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	return &dispResp, result
}

// Status fetches GET /dispense/{tx_id} (auth required)
func (c *DispenserClient) Status(txID string) (*DispenseResponse, APIResult) {
	start := time.Now()

	req, err := http.NewRequest("GET", c.BaseURL+"/dispense/"+txID, nil)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	req.Header.Set("X-API-Key", c.APIKey)

	resp, err := c.HTTPClient.Do(req)
	if err != nil {
		return nil, APIResult{Error: err, Latency: time.Since(start)}
	}
	defer resp.Body.Close()

	latency := time.Since(start)
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	result := APIResult{StatusCode: resp.StatusCode, Latency: latency}

	if resp.StatusCode == 404 {
		return nil, APIResult{StatusCode: 404, Error: fmt.Errorf("transaction not found"), Latency: latency}
	}

	if resp.StatusCode != 200 {
		return nil, APIResult{
			StatusCode: resp.StatusCode,
			Error:      fmt.Errorf("status returned %d: %s", resp.StatusCode, string(body)),
			Latency:    latency,
		}
	}

	var dispResp DispenseResponse
	if err := json.Unmarshal(body, &dispResp); err != nil {
		return nil, APIResult{StatusCode: resp.StatusCode, Error: err, Latency: latency}
	}

	return &dispResp, result
}

// CheckProtocol enforces the version handshake from dispenser-protocol.md.
// A device that reports anything but ProtocolVersion is refused outright.
func CheckProtocol(got int) error {
	if got == ProtocolVersion {
		return nil
	}
	if got == 0 {
		return fmt.Errorf("device reports no protocol version; this client speaks protocol %d only", ProtocolVersion)
	}
	return fmt.Errorf("device speaks protocol %d, this client speaks protocol %d only", got, ProtocolVersion)
}
