package main

import "time"

// HealthResponse matches GET /health from the dispenser protocol
type HealthResponse struct {
	Status       string        `json:"status"`
	Uptime       int           `json:"uptime"`
	Firmware     string        `json:"firmware"`
	WiFi         *WiFiInfo     `json:"wifi,omitempty"`
	Dispenser    string        `json:"dispenser"`
	GPIO         *GPIOInfo     `json:"gpio,omitempty"`
	Metrics      Metrics       `json:"metrics"`
	Error        *ErrorInfo    `json:"error"`
	ErrorHistory []ErrorRecord `json:"error_history"`
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

type Metrics struct {
	// Transaction-level metrics
	TotalDispenses int `json:"total_dispenses"`
	Successful     int `json:"successful"`
	Jams           int `json:"jams"`
	Partial        int `json:"partial"`
	Crashes        int `json:"crashes"`
	Failures       int `json:"failures"`

	// Token-level metrics
	RequestedTokens int `json:"requested_tokens"`
	DispensedTokens int `json:"dispensed_tokens"`
}

type ActiveTxInfo struct {
	TxID      string `json:"tx_id"`
	Quantity  int    `json:"quantity"`
	Dispensed int    `json:"dispensed"`
}

type ErrorInfo struct {
	Active      bool   `json:"active"`
	Code        int    `json:"code,omitempty"`
	Type        string `json:"type,omitempty"`
	Timestamp   int    `json:"timestamp,omitempty"`
	Description string `json:"description,omitempty"`
}

type ErrorRecord struct {
	Code      int    `json:"code"`
	Type      string `json:"type"`
	Timestamp int    `json:"timestamp"`
	Cleared   bool   `json:"cleared"`
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
	Error       string `json:"error"`
	ActiveTxID  string `json:"active_tx_id,omitempty"`
	ActiveState string `json:"active_state,omitempty"`
}

// Transaction represents a dispense transaction
type Transaction struct {
	TxID      string
	State     string // "idle", "dispensing", "done", "error"
	Quantity  int
	Dispensed int
	Timestamp time.Time
	StopChan  chan bool // For controlling dispensing goroutine
}
