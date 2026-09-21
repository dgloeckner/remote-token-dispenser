package main

import "time"

// ProtocolVersion is the version handshake reported by GET /health.
// It must match the version in dispenser-protocol.md and the firmware's
// PROTOCOL_VERSION; a client refuses any other value.
//
// --protocol overrides what the mock CLAIMS, without changing anything it
// does.  That is the only way to exercise a terminal's handshake against a
// device speaking the old version: there are none left in the field, and a
// client that quietly adapts to one is the bug the handshake exists to catch.
//
// Request signing is part of what protocol 2 IS (issue #8, owner decision
// 2026-09-21): `X-API-Key` never existed in a released protocol 2, because
// protocol 2 has never been released — #1 through #8 ship in it together.
const ProtocolVersion = 2

// Fault values, the device-level condition of issue #6.
const (
	FaultNone        = "none"
	FaultJam         = "jam"
	FaultHopperError = "hopper_error"
)

// Transaction error types (dispenser-protocol.md).  The Azkoyen names come
// from the error table; these are the two the firmware produces itself.
const (
	TxErrorNone       = "NONE"
	TxErrorJamTimeout = "JAM_TIMEOUT"
	TxErrorReset      = "RESET"
)

// HealthResponse matches GET /health from the dispenser protocol.
//
// One State and one Fault since issue #6.  Protocol 1 had `status`
// (hard-coded "ok" in the firmware) next to `dispenser`, which overlapped it,
// and the terminal ORed the two together.
type HealthResponse struct {
	Protocol int    `json:"protocol"`
	State    string `json:"state"`
	// Authenticated says which of the two documents this is (issue #8).
	// Stated rather than inferred: a client that forgot to sign must be able
	// to tell a reduced document from an old firmware that never had the
	// fields.
	Authenticated bool `json:"authenticated"`
	// Fault is the device-level condition: none | jam | hopper_error.  It
	// outlives the transaction it broke and is cleared by a reboot only.
	Fault string `json:"fault"`
	// FaultCode is the Azkoyen code behind a hopper_error, 0 otherwise.
	// No omitempty: the field is required, and 0 is a value.
	FaultCode int    `json:"fault_code"`
	Uptime    int    `json:"uptime"`
	Firmware  string `json:"firmware"`
	// HeapFree, ResetReason and WiFi.Reconnects are the operational telemetry
	// of issue #7.  The mock has no heap and never resets, but it reports the
	// fields: a client that reads them has to be exercisable without an ESP,
	// and a mock that leaves them out teaches the client to tolerate their
	// absence — which is the habit the pointer types exist to prevent.
	HeapFree     int           `json:"heap_free"`
	ResetReason  string        `json:"reset_reason"`
	WiFi         *WiFiInfo     `json:"wifi,omitempty"`
	Metrics      Metrics       `json:"metrics"`
	ErrorHistory []ErrorRecord `json:"error_history"`
}

// MinimalHealthResponse is what GET /health answers WITHOUT a signature
// (issue #8): is the machine there, can it sell (`state != "fault"`), does it
// need a human (`fault != "none"`) — and the protocol version, because the
// handshake has to be possible before a client can sign anything at all.
//
// Everything else needs the key.  `fault_code` is a diagnosis where `fault` is
// already the verdict; `uptime`/`reset_reason`/`heap_free` are a reboot oracle
// together; `firmware` is version fingerprinting; `wifi` leaks SSID and IP;
// `metrics` carries the lifetime `dispensed_tokens` the backend bills against
// (dgloeckner/clubbar#952); `error_history` is timestamped diagnosis.
type MinimalHealthResponse struct {
	Protocol      int    `json:"protocol"`
	State         string `json:"state"`
	Fault         string `json:"fault"`
	Authenticated bool   `json:"authenticated"`
}

// NonceResponse is GET /nonce.
type NonceResponse struct {
	Nonce string `json:"nonce"`
	TTL   int    `json:"ttl"`
}

// DebugResponse matches GET /debug: the raw pin levels, which left /health in
// issue #6.  Authenticated, because nothing a monitor should act on is in it.
type DebugResponse struct {
	GPIO GPIOInfo `json:"gpio"`
}

type WiFiInfo struct {
	RSSI int    `json:"rssi"`
	IP   string `json:"ip"`
	SSID string `json:"ssid"`
	// Reconnects counts the times the link came back since boot.  Always 0
	// here: the mock's link never drops, and saying so is a reading.
	Reconnects int `json:"reconnects"`
}

// GPIOInfo has no hopper_low any more (issue #6): the empty sensor is a
// factory option the hopper in the boathouse does not have, so the pin sat on
// its pull-up and said "not empty" forever — and /health published that.
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
	// OverrunTokens counts the tokens that left the hopper past the requested
	// quantity: the ones that fall while the disc coasts to a stop (issue #5).
	// They are billed, so they are counted — a device that clamps `dispensed`
	// at `quantity` hides them from everyone.
	OverrunTokens int `json:"overrun_tokens"`
}

type ActiveTxInfo struct {
	TxID      string `json:"tx_id"`
	Quantity  int    `json:"quantity"`
	Dispensed int    `json:"dispensed"`
}

// ErrorRecord is one decoded hopper error.  No `cleared` flag: an error
// raises a fault, a fault ends with a power cycle, and the "self-healing" the
// flag described had no case left to heal.
type ErrorRecord struct {
	Code      int    `json:"code"`
	Type      string `json:"type"`
	Timestamp int    `json:"timestamp"`
}

// DispenseRequest matches POST /dispense
type DispenseRequest struct {
	TxID     string `json:"tx_id"`
	Quantity int    `json:"quantity"`
}

// DispenseResponse matches dispense endpoint responses.
//
// CountReliable is required on every transaction response and therefore has no
// omitempty: false must be sent as false.  It says whether Dispensed is exact
// or only a lower bound — the device lost power mid-dispense and the live
// count in RTC memory went with it (dispenser-protocol.md, issue #3).
type DispenseResponse struct {
	TxID          string `json:"tx_id"`
	State         string `json:"state"`
	Quantity      int    `json:"quantity"`
	Dispensed     int    `json:"dispensed"`
	CountReliable bool   `json:"count_reliable"`
	// ErrorCode / ErrorType say WHY a transaction ended in "error" (issue
	// #6): an Azkoyen code 1-7 with its name, or 0 with JAM_TIMEOUT or RESET.
	// Required on every transaction response, NONE when nothing went wrong —
	// a reader must not have to tell "no error" from "no field".
	ErrorCode int    `json:"error_code"`
	ErrorType string `json:"error_type"`
	Error     string `json:"error,omitempty"`
}

// ErrorResponse for 4xx/5xx.  A 409 fault names the fault and its code, so
// the terminal can tell "clear the jam" from "the hopper reports a motor
// fault" — and never how to clear it, because there is no way but the plug.
type ErrorResponse struct {
	Error string `json:"error"`
	// Reason narrows a 401 (issue #8): "nonce" means fetch a fresh one from
	// GET /nonce and retry ONCE — safe, because every mutating request is
	// idempotent by tx_id.  "signature" means the request is wrong and a
	// retry changes nothing.
	Reason      string `json:"reason,omitempty"`
	ActiveTxID  string `json:"active_tx_id,omitempty"`
	ActiveState string `json:"active_state,omitempty"`
	Fault       string `json:"fault,omitempty"`
	FaultCode   int    `json:"fault_code,omitempty"`
}

// Transaction represents a dispense transaction
type Transaction struct {
	TxID      string
	State     string // "idle", "dispensing", "done", "error"
	Quantity  int
	Dispensed int
	// CountReliable mirrors the firmware: true unless the transaction was
	// recovered after a reset that took the live count with it.
	CountReliable bool
	// ErrorKind/ErrorCode mirror the firmware's per-transaction reason.
	ErrorType string
	ErrorCode int
	Timestamp time.Time
	StopChan  chan bool // For controlling dispensing goroutine
}
