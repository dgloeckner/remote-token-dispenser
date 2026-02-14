package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// HealthResponse matches GET /health from the dispenser protocol
type HealthResponse struct {
	Status    string        `json:"status"`
	Uptime    int           `json:"uptime"`
	Firmware  string        `json:"firmware"`
	WiFi      *WiFiInfo     `json:"wifi,omitempty"`
	Dispenser string        `json:"dispenser"`
	GPIO      *GPIOInfo     `json:"gpio,omitempty"`
	Metrics   Metrics       `json:"metrics"`
	ActiveTx  *ActiveTxInfo `json:"active_tx,omitempty"`
}

type Metrics struct {
	TotalDispenses int    `json:"total_dispenses"`
	Successful     int    `json:"successful"`
	Jams           int    `json:"jams"`
	Partial        int    `json:"partial"`
	Failures       int    `json:"failures"`
	LastError      string `json:"last_error"`
	LastErrorType  string `json:"last_error_type"`
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
}

type GPIOInfo struct {
	CoinPulse struct {
		Raw    int  `json:"raw"`
		Active bool `json:"active"`
	} `json:"coin_pulse"`
	ErrorSignal struct {
		Raw    int  `json:"raw"`
		Active bool `json:"active"`
	} `json:"error_signal"`
	HopperLow struct {
		Raw    int  `json:"raw"`
		Active bool `json:"active"`
	} `json:"hopper_low"`
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
	Error     string `json:"error,omitempty"`
}

// ErrorResponse for 4xx/5xx
type ErrorResponse struct {
	Error     string `json:"error"`
	ActiveTxID string `json:"active_tx_id,omitempty"`
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

	return &health, APIResult{StatusCode: 200, Latency: latency}
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
		var errResp ErrorResponse
		json.Unmarshal(body, &errResp)
		return nil, APIResult{
			StatusCode: 409,
			Error:      fmt.Errorf("busy: active tx %s", errResp.ActiveTxID),
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
