package main

// The conformance table: dispenser-protocol.md turned into assertions.
//
// Rules for adding a case:
//   - Assert the PROTOCOL, not an implementation. If the mock and the firmware
//     disagree, that is the finding — do not weaken the case to make both pass.
//   - A case that today fails against one target carries a Note naming the
//     issue that fixes it, so a red line reads as "known" or "new" at a glance.
//   - A case that leaves an unclearable state (hardware error) is Destructive
//     and runs last.

import (
	"fmt"
	"strings"
	"time"
)

func ConformanceCases() []Case {
	return []Case{
		// --- health / handshake -------------------------------------------
		{
			Name: "health_is_200_without_auth",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/health", "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 200)
			},
		},
		{
			Name: "health_protocol_is_2",
			Note: "the version handshake from dispenser-protocol.md; a device without it is protocol 1",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				return CheckProtocol(health.Protocol)
			},
		},
		{
			Name: "health_schema_has_required_fields",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				var missing []string
				if health.Status == "" {
					missing = append(missing, "status")
				}
				if health.Firmware == "" {
					missing = append(missing, "firmware")
				}
				if health.Dispenser == "" {
					missing = append(missing, "dispenser")
				}
				if len(missing) > 0 {
					return fmt.Errorf("missing field(s): %s", strings.Join(missing, ", "))
				}
				switch health.Dispenser {
				case "idle", "dispensing", "done", "error":
				default:
					return fmt.Errorf("dispenser state %q is not in the protocol's enum", health.Dispenser)
				}
				return nil
			},
		},

		// --- authentication ------------------------------------------------
		{
			Name: "post_without_api_key_is_401",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"noauth01","quantity":1}`, false)
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "post_with_wrong_api_key_is_401",
			Run: func(c *Ctx) error {
				got, err := c.raw("POST", "/dispense", `{"tx_id":"wrongkey","quantity":1}`,
					map[string]string{"Content-Type": "application/json", "X-API-Key": "definitely-not-the-key"})
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "get_status_without_api_key_is_401",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/dispense/whatever", "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},

		// --- request validation ---------------------------------------------
		{
			Name: "post_with_invalid_json_is_400",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":`, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 400)
			},
		},
		{
			Name: "post_without_json_content_type_is_415",
			Run: func(c *Ctx) error {
				got, err := c.raw("POST", "/dispense", `{"tx_id":"ct01","quantity":1}`,
					map[string]string{"Content-Type": "text/plain", "X-API-Key": c.Client.APIKey})
				if err != nil {
					return err
				}
				return wantStatus(got, 415)
			},
		},
		{
			Name: "post_with_empty_tx_id_is_400",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"","quantity":1}`, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 400)
			},
		},
		{
			Name: "post_with_overlong_tx_id_is_400",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"0123456789abcdefg","quantity":1}`, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 400)
			},
		},
		{
			Name: "post_with_quantity_zero_is_400",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"qty0","quantity":0}`, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 400)
			},
		},
		{
			Name: "post_with_quantity_above_max_is_400",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"qty21","quantity":21}`, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 400)
			},
		},
		{
			Name: "get_unknown_tx_is_404",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/dispense/"+c.NextTxID("unk"), "",
					map[string]string{"X-API-Key": c.Client.APIKey})
				if err != nil {
					return err
				}
				return wantStatus(got, 404)
			},
		},

		// --- state machine ---------------------------------------------------
		{
			Name: "dispense_one_token_reaches_done",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("ok")
				resp, res := c.Client.Dispense(txID, 1)
				if res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				if resp.State != "dispensing" && resp.State != "done" {
					return fmt.Errorf("POST answered state %q, expected dispensing or done", resp.State)
				}
				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.State != "done" {
					return fmt.Errorf("final state %q, expected done", final.State)
				}
				if final.Dispensed != 1 {
					return fmt.Errorf("dispensed %d, expected 1", final.Dispensed)
				}
				return nil
			},
		},
		{
			Name: "replay_of_finished_tx_is_200_and_does_not_redispense",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("rp")
				if _, res := c.Client.Dispense(txID, 1); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				if _, err := c.waitForFinalState(txID, 30*time.Second); err != nil {
					return err
				}
				replay, res := c.Client.Dispense(txID, 1)
				if res.Error != nil {
					return fmt.Errorf("replay failed: %v", res.Error)
				}
				if replay.State != "done" {
					return fmt.Errorf("replay answered state %q, expected the cached done", replay.State)
				}
				if replay.Dispensed != 1 {
					return fmt.Errorf("replay reports dispensed %d, expected the cached 1", replay.Dispensed)
				}
				return nil
			},
		},
		{
			Name: "post_other_tx_while_dispensing_is_409",
			Run: func(c *Ctx) error {
				busy := c.NextTxID("bs")
				if _, res := c.Client.Dispense(busy, c.SlowQuantity()); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, c.NextTxID("bs")), true)
				if err != nil {
					return err
				}
				defer func() { _, _ = c.waitForFinalState(busy, 60*time.Second) }()
				return wantStatus(got, 409)
			},
		},
		{
			Name: "post_retry_while_dispensing_is_200",
			Note: "RED against the firmware until #2: startDispense() looks only in the history ring, " +
				"so a retry of the running transaction is answered 409 busy",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("rt")
				if _, res := c.Client.Dispense(txID, c.SlowQuantity()); res.Error != nil {
					return fmt.Errorf("first POST failed: %v", res.Error)
				}
				defer func() { _, _ = c.waitForFinalState(txID, 60*time.Second) }()

				got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":%d}`, txID, c.SlowQuantity()), true)
				if err != nil {
					return err
				}
				if err := wantStatus(got, 200); err != nil {
					return fmt.Errorf("retry of the ACTIVE tx must return its state: %w", err)
				}
				if !strings.Contains(got.Body, txID) {
					return fmt.Errorf("retry answered a body without the tx_id: %s", got.Body)
				}
				return nil
			},
		},

		// --- hardware error --------------------------------------------------
		{
			Name:        "post_while_error_is_409",
			Targets:     []Target{TargetMock, TargetSimulator},
			Destructive: true,
			Note: "RED against the firmware until #6: only STATE_DISPENSING blocks a new POST, " +
				"so an active hardware error is silently cleared by the next request",
			Run: func(c *Ctx) error {
				if err := c.induceHardwareError(); err != nil {
					return err
				}
				got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, c.NextTxID("er")), true)
				if err != nil {
					return err
				}
				return wantStatus(got, 409)
			},
		},

		// --- cases that need a human or a switch ------------------------------
		{
			Name:             "reset_mid_dispense_reports_partial_count",
			Targets:          []Target{TargetSimulator, TargetHopper},
			NeedsInteraction: true,
			Note:             "see #3: the count is persisted once, at zero, so this reports dispensed=0 today",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("rs")
				if _, res := c.Client.Dispense(txID, 5); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				c.Prompt("press RST on the dispenser ESP now, while tokens are dropping")
				// Give the device time to boot and rejoin the WLAN.
				time.Sleep(10 * time.Second)
				tx, res := c.Client.Status(txID)
				if res.Error != nil {
					return fmt.Errorf("status after reset: %v", res.Error)
				}
				if tx.State != "error" {
					return fmt.Errorf("state after a reset mid-dispense is %q, expected error", tx.State)
				}
				if tx.Dispensed == 0 {
					return fmt.Errorf("dispensed=0 after a reset mid-dispense; the partial count was lost")
				}
				return nil
			},
		},
	}
}

// induceHardwareError puts the target into an active hardware error.
// The mock maps quantity 8 to COIN_STUCK; on the simulator the operator sets
// the error switch (docs/hopper-simulator.md).
func (c *Ctx) induceHardwareError() error {
	switch c.Target {
	case TargetMock:
		txID := c.NextTxID("hw")
		if _, res := c.Client.Dispense(txID, 8); res.Error != nil {
			return fmt.Errorf("could not trigger the mock's COIN_STUCK scenario: %v", res.Error)
		}
		deadline := time.Now().Add(5 * time.Second)
		for time.Now().Before(deadline) {
			health, _ := c.Client.Health()
			if health != nil && health.Error != nil && health.Error.Active {
				return nil
			}
			time.Sleep(100 * time.Millisecond)
		}
		return fmt.Errorf("mock did not report an active error after the COIN_STUCK scenario")
	case TargetSimulator:
		if !c.Interactive {
			return fmt.Errorf("set the simulator's error switch first; run with --interactive")
		}
		c.Prompt("set the hopper simulator's ERROR switch to code 1 (COIN_STUCK)")
		return nil
	default:
		return fmt.Errorf("no way to induce a hardware error on target %s", c.Target)
	}
}
