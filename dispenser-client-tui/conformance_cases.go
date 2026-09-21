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
	"encoding/json"
	"fmt"
	"sort"
	"strings"
	"time"
)

func ConformanceCases() []Case {
	return []Case{
		// --- health / handshake -------------------------------------------
		{
			Name: "health_is_200_without_auth",
			Note: "still true after issue #8, and deliberately: /health unsigned is the " +
				"liveness probe, so it answers 200 with three fields rather than 401",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/health", "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 200)
			},
		},
		{
			Name: "health_protocol_is_3",
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
			Note: "protocol 2 replaced the overlapping status/dispenser pair with one " +
				"state and one fault (issue #6); a device that still sends the old " +
				"pair leaves the terminal guessing which of the two wins",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				var missing []string
				if health.Firmware == "" {
					missing = append(missing, "firmware")
				}
				if health.State == "" {
					missing = append(missing, "state")
				}
				if health.Fault == "" {
					missing = append(missing, "fault")
				}
				if health.FaultCode == nil {
					missing = append(missing, "fault_code")
				}
				if len(missing) > 0 {
					return fmt.Errorf("missing field(s): %s", strings.Join(missing, ", "))
				}
				switch health.State {
				case "idle", "dispensing", "fault":
				default:
					return fmt.Errorf("device state %q is not in the protocol's enum "+
						"(idle | dispensing | fault)", health.State)
				}
				switch health.Fault {
				case "none", "jam", "hopper_error":
				default:
					return fmt.Errorf("fault %q is not in the protocol's enum "+
						"(none | jam | hopper_error)", health.Fault)
				}
				return nil
			},
		},
		{
			Name: "health_has_no_hopper_low",
			Note: "the empty sensor is a factory option our hopper does not have, so the " +
				"pin said 'fine' forever; a field that is always fine is worse than no " +
				"field, and it was removed from the protocol in issue #6",
			Run: func(c *Ctx) error {
				// Signed: since #8 the raw pin block, if a device still had
				// one, would be in the authenticated document.
				got, err := c.rawSigned("GET", "/health", "", nil)
				if err != nil {
					return err
				}
				if strings.Contains(got.Body, "hopper_low") {
					return fmt.Errorf("/health still publishes hopper_low: %s",
						strings.TrimSpace(got.Body))
				}
				return nil
			},
		},
		{
			Name: "health_reports_ops_telemetry",
			Note: "issue #7: without free heap, the reset reason and a reconnect count, a " +
				"device that resets once a week leaves no evidence but a small uptime",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				var missing []string
				if health.HeapFree == nil {
					missing = append(missing, "heap_free")
				}
				if health.ResetReason == "" {
					missing = append(missing, "reset_reason")
				}
				if health.WiFi == nil {
					missing = append(missing, "wifi")
				} else if health.WiFi.Reconnects == nil {
					missing = append(missing, "wifi.reconnects")
				}
				if len(missing) > 0 {
					return fmt.Errorf("missing field(s): %s", strings.Join(missing, ", "))
				}
				// Zero free heap is not a reading, it is a device that would
				// already be dead; the field is there but says nothing.
				if *health.HeapFree <= 0 {
					return fmt.Errorf("heap_free is %d", *health.HeapFree)
				}
				return nil
			},
		},
		{
			Name: "no_reset_route_exists",
			Note: "a fault is cleared by a power cycle and by nothing else (owner " +
				"decision, 2026-09-20): no reset endpoint, none in the TUI, none on the kiosk",
			Run: func(c *Ctx) error {
				got, err := c.rawSigned("POST", "/reset", "",
					map[string]string{"Content-Type": "application/json"})
				if err != nil {
					return err
				}
				if got.Status == 200 || got.Status == 204 {
					return fmt.Errorf("POST /reset answered %d: the device has a way out of "+
						"a fault that is not a power cycle", got.Status)
				}
				return nil
			},
		},

		// --- authentication: request signing (issue #8) ---------------------
		//
		// `X-API-Key` is GONE.  Until #8 the shared secret travelled in clear
		// in every request, so one listener on the WLAN owned the machine and
		// a captured request replayed with a new tx_id dispensed again.  The
		// replacement is HMAC-SHA256 over METHOD \n PATH \n BODY \n NONCE with
		// a single-use nonce from GET /nonce.
		{
			Name: "nonce_endpoint_hands_out_a_fresh_nonce",
			Note: "issue #8: a client holds nothing it can sign with until it has one, " +
				"so GET /nonce is unauthenticated by necessity",
			Run: func(c *Ctx) error {
				first, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				if len(first) != NonceHexLen {
					return fmt.Errorf("nonce %q is %d characters, want %d", first, len(first), NonceHexLen)
				}
				second, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				if first == second {
					// A constant "nonce" is a constant, and a replay defence
					// built on it defends nothing.
					return fmt.Errorf("two calls to /nonce returned the same value %q", first)
				}
				return nil
			},
		},
		{
			Name: "post_without_a_signature_is_401",
			Run: func(c *Ctx) error {
				got, err := c.postJSON(`{"tx_id":"noauth01","quantity":1}`, false)
				if err != nil {
					return err
				}
				if err := wantStatus(got, 401); err != nil {
					return err
				}
				return wantReason(got, "signature")
			},
		},
		{
			Name: "post_with_the_old_api_key_header_is_401",
			Note: "the header of protocol 2 is not a credential and not a fallback: " +
				"a request carrying it is simply unsigned",
			Run: func(c *Ctx) error {
				got, err := c.raw("POST", "/dispense", `{"tx_id":"oldkey01","quantity":1}`,
					map[string]string{"Content-Type": "application/json",
						"X-API-Key": c.Client.SigningKey})
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "post_signed_with_the_wrong_key_is_401",
			Run: func(c *Ctx) error {
				nonce, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				body := `{"tx_id":"wrongkey","quantity":1}`
				got, err := c.raw("POST", "/dispense", body, map[string]string{
					"Content-Type": "application/json",
					"X-Nonce":      nonce,
					"X-Signature":  SignRequest("definitely-not-the-key", "POST", "/dispense", body, nonce),
				})
				if err != nil {
					return err
				}
				if err := wantStatus(got, 401); err != nil {
					return err
				}
				return wantReason(got, "signature")
			},
		},
		{
			Name: "replayed_signed_post_is_401",
			Note: "the replay this whole scheme exists to stop: identical bytes, " +
				"identical signature, a second time",
			Run: func(c *Ctx) error {
				nonce, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				body := fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, c.NextTxID("rep"))
				headers := map[string]string{
					"Content-Type": "application/json",
					"X-Nonce":      nonce,
					"X-Signature":  SignRequest(c.Client.SigningKey, "POST", "/dispense", body, nonce),
				}
				first, err := c.raw("POST", "/dispense", body, headers)
				if err != nil {
					return err
				}
				if err := wantStatus(first, 200); err != nil {
					return fmt.Errorf("the first signed POST was refused: %w", err)
				}
				second, err := c.raw("POST", "/dispense", body, headers)
				if err != nil {
					return err
				}
				if err := wantStatus(second, 401); err != nil {
					return err
				}
				return wantReason(second, "nonce")
			},
		},
		{
			Name: "tampered_body_is_401",
			Note: "a captured request re-aimed at more tokens",
			Run: func(c *Ctx) error {
				nonce, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				id := c.NextTxID("tam")
				signedBody := fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, id)
				sentBody := fmt.Sprintf(`{"tx_id":%q,"quantity":9}`, id)
				got, err := c.raw("POST", "/dispense", sentBody, map[string]string{
					"Content-Type": "application/json",
					"X-Nonce":      nonce,
					"X-Signature":  SignRequest(c.Client.SigningKey, "POST", "/dispense", signedBody, nonce),
				})
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "signature_covers_method_and_path",
			Note: "method and path are in the canonical string so a status poll's " +
				"signature cannot be lifted onto /debug or onto a POST",
			Run: func(c *Ctx) error {
				nonce, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				// A signature over a canonical string with method and path left
				// out — what a device that binds only the body would accept.
				// The request is otherwise perfect, so a 200 here means the
				// signature says nothing about WHICH request it belongs to.
				got, err := c.raw("GET", "/debug", "", map[string]string{
					"X-Nonce":     nonce,
					"X-Signature": SignRequest(c.Client.SigningKey, "", "", "", nonce),
				})
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "a_spent_nonce_still_polls",
			Note: "a POST spends its nonce for MUTATING use only — the status polls " +
				"behind it keep working, which is what one GET /nonce per transaction means",
			Run: func(c *Ctx) error {
				nonce, err := c.Client.fetchNonce()
				if err != nil {
					return err
				}
				id := c.NextTxID("spn")
				body := fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, id)
				got, err := c.raw("POST", "/dispense", body, map[string]string{
					"Content-Type": "application/json",
					"X-Nonce":      nonce,
					"X-Signature":  SignRequest(c.Client.SigningKey, "POST", "/dispense", body, nonce),
				})
				if err != nil {
					return err
				}
				// 200 or 409: whether the device happened to be busy with an
				// earlier case's transaction is not what this asserts.  Either
				// way the signature verified, so the nonce is spent.
				if got.Status != 200 && got.Status != 409 {
					return wantStatus(got, 200)
				}
				poll, err := c.raw("GET", "/dispense/"+id, "", map[string]string{
					"X-Nonce":     nonce,
					"X-Signature": SignRequest(c.Client.SigningKey, "GET", "/dispense/"+id, "", nonce),
				})
				if err != nil {
					return err
				}
				if poll.Status == 401 {
					return fmt.Errorf("polling with the nonce the POST spent is 401: a "+
						"transaction would need a fresh nonce for every poll (body: %s)",
						strings.TrimSpace(poll.Body))
				}
				return nil
			},
		},
		{
			Name: "get_status_without_a_signature_is_401",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/dispense/whatever", "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "get_debug_without_a_signature_is_401",
			Run: func(c *Ctx) error {
				got, err := c.raw("GET", "/debug", "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 401)
			},
		},
		{
			Name: "health_without_a_signature_is_minimal",
			Note: "issue #8: unsigned, /health answers only protocol, state and fault — " +
				"is it there, can it sell, does it need a human.  SSID, IP, firmware " +
				"version, heap, reset reason and the billed token counts need the key.",
			Run: func(c *Ctx) error {
				status, body, err := c.Client.HealthUnsigned()
				if err != nil {
					return err
				}
				if status != 200 {
					// Not 401: it is the liveness probe, and a 401 answers
					// neither "the machine is there" nor "it is gone".
					return fmt.Errorf("unsigned GET /health answered %d, want 200", status)
				}
				var minimal struct {
					Protocol      int    `json:"protocol"`
					State         string `json:"state"`
					Fault         string `json:"fault"`
					Authenticated *bool  `json:"authenticated"`
				}
				if err := json.Unmarshal(body, &minimal); err != nil {
					return fmt.Errorf("unsigned /health is not JSON: %w", err)
				}
				if minimal.State == "" || minimal.Fault == "" || minimal.Protocol == 0 {
					return fmt.Errorf("the unsigned document is missing one of its three fields: %s",
						strings.TrimSpace(string(body)))
				}
				if minimal.Authenticated == nil {
					return fmt.Errorf(`the unsigned document does not say "authenticated": false, ` +
						"so a client cannot tell it from an old firmware that never had the fields")
				}
				if *minimal.Authenticated {
					return fmt.Errorf("the unsigned document claims to be authenticated")
				}
				var leaked []string
				for _, field := range []string{"wifi", "ssid", "metrics", "heap_free",
					"reset_reason", "firmware", "uptime", "error_history", "fault_code"} {
					if strings.Contains(string(body), `"`+field+`"`) {
						leaked = append(leaked, field)
					}
				}
				if len(leaked) > 0 {
					return fmt.Errorf("the unsigned /health carries %s", strings.Join(leaked, ", "))
				}
				return nil
			},
		},
		{
			Name: "health_with_a_signature_is_whole",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.Authenticated == nil || !*health.Authenticated {
					return fmt.Errorf("a signed /health does not say it is authenticated")
				}
				if health.WiFi == nil || health.Firmware == "" {
					return fmt.Errorf("the signed document is missing the fields the key buys")
				}
				return nil
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
				got, err := c.rawSigned("POST", "/dispense", `{"tx_id":"ct01","quantity":1}`,
					map[string]string{"Content-Type": "text/plain"})
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
				id := c.NextTxID("unk")
				got, err := c.rawSigned("GET", "/dispense/"+id, "", nil)
				if err != nil {
					return err
				}
				return wantStatus(got, 404)
			},
		},

		// --- the request itself: body framing and latency (issue #4) ---------
		//
		// The POST handler runs in the TCP/SDK callback.  Three consequences the
		// terminal actually sees, and all three are protocol, not implementation:
		// a request must always be answered, a body may arrive in pieces, and an
		// answer must not wait for a flash erase and 500 bytes of serial output.
		{
			Name: "post_without_body_is_400",
			Note: "an unanswered request is worse than a rejected one: the terminal " +
				"waits out its own timeout and then retries a transaction it cannot " +
				"tell apart from a lost one",
			Run: func(c *Ctx) error {
				start := time.Now()
				got, err := c.postJSON("", true)
				if err != nil {
					return fmt.Errorf("POST with an empty body was never answered: %w", err)
				}
				if err := wantStatus(got, 400); err != nil {
					return err
				}
				if elapsed := time.Since(start); elapsed > time.Second {
					return fmt.Errorf("answered after %s; an empty body is rejected from "+
						"the request handler, before any work", elapsed.Round(time.Millisecond))
				}
				return nil
			},
		},
		{
			Name: "post_body_in_two_segments_is_accepted",
			Note: "TCP does not promise one segment per body; a device that parses " +
				"whatever chunk it was handed answers 400 at random",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("sg")
				body := fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, txID)
				got, err := c.postInTwoSegments(body)
				if err != nil {
					return err
				}
				if err := wantStatus(got, 200); err != nil {
					return fmt.Errorf("a body split across two TCP segments must be "+
						"assembled before it is parsed: %w", err)
				}
				if !strings.Contains(got.Body, txID) {
					return fmt.Errorf("answer does not name the transaction: %s", strings.TrimSpace(got.Body))
				}
				_, _ = c.waitForFinalState(txID, 30*time.Second)
				return nil
			},
		},
		{
			Name: "post_with_oversized_body_is_413",
			Note: "a body past the device's 256-byte cap is refused as too large, " +
				"not truncated into a parse error that blames the JSON",
			Run: func(c *Ctx) error {
				body := fmt.Sprintf(`{"tx_id":%q,"quantity":1,"pad":%q}`,
					c.NextTxID("big"), strings.Repeat("x", 300))
				got, err := c.postJSON(body, true)
				if err != nil {
					return err
				}
				return wantStatus(got, 413)
			},
		},
		{
			Name: "post_latency_p95_below_300ms",
			Note: "the bench number from issue #4: the POST answers from memory, " +
				"the flash commit and the motor start happen in loop()",
			Run: func(c *Ctx) error {
				samples := c.latencySamples()
				latencies := make([]time.Duration, 0, samples)
				for i := 0; i < samples; i++ {
					txID := c.NextTxID("lt")
					start := time.Now()
					got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, txID), true)
					latencies = append(latencies, time.Since(start))
					if err != nil {
						return fmt.Errorf("POST %d failed: %w", i+1, err)
					}
					if err := wantStatus(got, 200); err != nil {
						return fmt.Errorf("POST %d: %w", i+1, err)
					}
					// One transaction at a time: a 409 busy would time the wrong thing.
					if _, err := c.waitForFinalState(txID, 30*time.Second); err != nil {
						return err
					}
				}
				sort.Slice(latencies, func(i, j int) bool { return latencies[i] < latencies[j] })
				p95 := latencies[(len(latencies)*95-1)/100]
				if p95 > 300*time.Millisecond {
					return fmt.Errorf("p95 POST latency is %s over %d requests, budget is 300ms "+
						"(median %s, max %s)", p95.Round(time.Millisecond), samples,
						latencies[len(latencies)/2].Round(time.Millisecond),
						latencies[len(latencies)-1].Round(time.Millisecond))
				}
				return nil
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
		{
			Name: "post_same_id_different_quantity_is_409",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("ru")
				if _, res := c.Client.Dispense(txID, c.SlowQuantity()); res.Error != nil {
					return fmt.Errorf("first POST failed: %v", res.Error)
				}
				defer func() { _, _ = c.waitForFinalState(txID, 60*time.Second) }()

				got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, txID), true)
				if err != nil {
					return err
				}
				if err := wantStatus(got, 409); err != nil {
					return fmt.Errorf("a known tx_id with another quantity is not a retry: %w", err)
				}
				if !strings.Contains(got.Body, "tx_id reused") {
					return fmt.Errorf("409 body does not say tx_id reused: %s", got.Body)
				}
				return nil
			},
		},
		{
			Name: "replay_old_tx_during_dispense_does_not_orphan_active",
			Run: func(c *Ctx) error {
				// The half of #2 that nobody sees: an idempotent hit used to
				// overwrite the active transaction, which switched the jam
				// watchdog off while the motor was running.
				old := c.NextTxID("ol")
				if _, res := c.Client.Dispense(old, 1); res.Error != nil {
					return fmt.Errorf("POST of the first tx failed: %v", res.Error)
				}
				if _, err := c.waitForFinalState(old, 30*time.Second); err != nil {
					return err
				}

				running := c.NextTxID("rn")
				if _, res := c.Client.Dispense(running, c.SlowQuantity()); res.Error != nil {
					return fmt.Errorf("POST of the running tx failed: %v", res.Error)
				}
				if _, res := c.Client.Dispense(old, 1); res.Error != nil {
					return fmt.Errorf("replay of the finished tx failed: %v", res.Error)
				}

				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.State != "dispensing" {
					return fmt.Errorf("device reports %q while a transaction is running; "+
						"the replay took the active transaction with it", health.State)
				}
				final, err := c.waitForFinalState(running, 60*time.Second)
				if err != nil {
					return err
				}
				if final.State != "done" {
					return fmt.Errorf("the running transaction ended in %q, expected done", final.State)
				}
				return nil
			},
		},

		// --- the count and the history across a reset (issue #3) --------------
		{
			Name: "count_reliable_is_present_on_every_transaction",
			Note: "a required field, not an optional one: a reader with a default " +
				"would turn 'we do not know' into 'we counted zero'",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("cr")
				if _, res := c.Client.Dispense(txID, 1); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.CountReliable == nil {
					return fmt.Errorf("the transaction response has no count_reliable field")
				}
				if !*final.CountReliable {
					return fmt.Errorf("a dispense that nothing interrupted reports count_reliable=false")
				}
				raw, err := c.rawSigned("GET", "/dispense/"+txID, "", nil)
				if err != nil {
					return err
				}
				if !strings.Contains(raw.Body, "count_reliable") {
					return fmt.Errorf("GET body carries no count_reliable: %s", strings.TrimSpace(raw.Body))
				}
				return nil
			},
		},
		{
			Name:    "failed_tx_carries_error_code_and_type",
			Targets: []Target{TargetMock},
			Note: "one flat 'error' made a jam, an empty hopper and a dead sensor the same " +
				"row on the terminal; since issue #6 the transaction says which it was. " +
				"The scenario here is the reset, because it is the one failure that " +
				"leaves the device usable — a jam would fault the mock for every case after it",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("ec")
				// The crash scenario: a transaction the reboot closed as an error.
				_, _ = c.Client.Dispense(txID, crashQuantity)
				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.State != "error" {
					return fmt.Errorf("state %q, expected error from the jam scenario", final.State)
				}
				if final.ErrorCode == nil {
					return fmt.Errorf("the failed transaction carries no error_code field")
				}
				if final.ErrorType == "" {
					return fmt.Errorf("the failed transaction carries no error_type field")
				}
				if final.ErrorType == "NONE" {
					return fmt.Errorf("a failed transaction reports error_type NONE: " +
						"the terminal cannot tell a jam from a motor fault")
				}
				return nil
			},
		},
		{
			Name: "successful_tx_carries_error_type_none",
			Note: "the pair is required on every transaction response, not only on the " +
				"failures: a reader with a default cannot tell 'no error' from 'no field'",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("en")
				if _, res := c.Client.Dispense(txID, 1); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.ErrorCode == nil || final.ErrorType == "" {
					return fmt.Errorf("a completed transaction is missing error_code/error_type")
				}
				if *final.ErrorCode != 0 || final.ErrorType != "NONE" {
					return fmt.Errorf("a completed transaction reports error_code=%d error_type=%q, "+
						"expected 0/NONE", *final.ErrorCode, final.ErrorType)
				}
				return nil
			},
		},
		{
			Name:    "crashed_tx_is_found_after_reboot",
			Targets: []Target{TargetMock},
			Note: "the device's own crash scenario; on a real ESP this is the " +
				"interactive reset_mid_dispense case below",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("cx")
				// The crash scenario drops the connection mid-response — the
				// transport error is the scenario, not a failure of the case.
				_, _ = c.Client.Dispense(txID, crashQuantity)

				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.State != "error" {
					return fmt.Errorf("state after the crash is %q, expected error", final.State)
				}
				if final.Dispensed == 0 {
					return fmt.Errorf("dispensed=0 after a crash that dropped a token; " +
						"the tokens in the tray are never billed")
				}
				if final.CountReliable == nil || !*final.CountReliable {
					return fmt.Errorf("a reset the RTC memory survives must report count_reliable=true")
				}
				// Issue #6: a recovered crash is not a fault.  One watchdog reset
				// used to take the machine out of service, and the dispense that
				// would have cleared it was exactly what the terminal refused to
				// start.
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.Fault != "none" || health.State == "fault" {
					return fmt.Errorf("the device reports state=%q fault=%q after a reset "+
						"mid-dispense; nothing is wrong with it and it must be sellable again "+
						"without anyone touching it", health.State, health.Fault)
				}
				return nil
			},
		},
		{
			Name:    "power_loss_reports_count_unreliable",
			Targets: []Target{TargetMock},
			Note:    "the same recovery without RTC memory: a lower bound that says it is one",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("pw")
				_, _ = c.Client.Dispense(txID, powerLossQuantity)

				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.State != "error" {
					return fmt.Errorf("state after the power loss is %q, expected error", final.State)
				}
				if final.CountReliable == nil {
					return fmt.Errorf("the transaction response has no count_reliable field")
				}
				if *final.CountReliable {
					return fmt.Errorf("count_reliable=true after a power loss: " +
						"the device is claiming a count it cannot have")
				}
				return nil
			},
		},

		// --- the pulse count: noise and overrun (issue #5) --------------------
		//
		// The device counts coins on a falling edge, and two things used to be
		// wrong with that number in opposite directions: electrical noise added
		// tokens that never fell (a short dispense), and a token that fell after
		// the motor stop was counted by the ISR and then discarded, because
		// `dispensed` was clamped at `quantity`.  The terminal bills `dispensed`,
		// so both are money.
		{
			Name:    "overrun_is_reported_above_quantity",
			Targets: []Target{TargetMock},
			Note: "a token that falls while the disc coasts is billed: dispensed > quantity " +
				"is legal (issue #5); on a real device this is the interactive coast case",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("ov")
				if _, res := c.Client.Dispense(txID, overrunQuantity); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				final, err := c.waitForFinalState(txID, 30*time.Second)
				if err != nil {
					return err
				}
				if final.State != "done" {
					return fmt.Errorf("state %q after an overrun, expected done: one token too "+
						"many is an accounting fact, not a fault", final.State)
				}
				if final.Dispensed <= final.Quantity {
					return fmt.Errorf("dispensed %d for a quantity of %d: the token that fell "+
						"after the motor stop was counted and then thrown away",
						final.Dispensed, final.Quantity)
				}
				if final.CountReliable == nil || !*final.CountReliable {
					return fmt.Errorf("an overrun is an exact count, not a lower bound")
				}
				return nil
			},
		},
		{
			Name: "health_reports_overrun_tokens",
			Note: "without the metric an overrun is only visible to whoever reads a single " +
				"transaction; nobody watches those",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.Metrics.OverrunTokens == nil {
					return fmt.Errorf("metrics carry no overrun_tokens field")
				}
				return nil
			},
		},
		{
			Name:             "bounce_burst_counts_one_token_per_coin",
			Targets:          []Target{TargetSimulator},
			NeedsInteraction: true,
			Note: "the simulator's bounce mode is the electrical noise of issue #5: a device " +
				"that counts raw edges reaches the target early and dispenses short",
			Run: func(c *Ctx) error {
				c.Prompt("type 'b' into the hopper simulator (bounce burst per token)")
				defer c.Prompt("type 'n' into the hopper simulator (back to normal pulses)")

				txID := c.NextTxID("bo")
				const quantity = 3
				if _, res := c.Client.Dispense(txID, quantity); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				final, err := c.waitForFinalState(txID, 60*time.Second)
				if err != nil {
					return err
				}
				if final.State != "done" {
					return fmt.Errorf("state %q with a bouncing sensor, expected done", final.State)
				}
				if final.Dispensed != quantity {
					return fmt.Errorf("dispensed %d of %d: the bounce edges were counted as tokens",
						final.Dispensed, quantity)
				}
				return nil
			},
		},
		{
			Name:             "coast_pulse_is_counted_as_an_overrun",
			Targets:          []Target{TargetSimulator},
			NeedsInteraction: true,
			Note:             "the simulator drops one extra token 120 ms after the motor stop; it must be billed",
			Run: func(c *Ctx) error {
				c.Prompt("type 'c' into the hopper simulator (one coast pulse after the stop)")
				defer c.Prompt("type 'n' into the hopper simulator (back to normal pulses)")

				txID := c.NextTxID("co")
				const quantity = 2
				if _, res := c.Client.Dispense(txID, quantity); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				final, err := c.waitForFinalState(txID, 60*time.Second)
				if err != nil {
					return err
				}
				if final.Dispensed != quantity+1 {
					return fmt.Errorf("dispensed %d, expected %d: the coast token is in the tray "+
						"either way, the question is only whether it is billed",
						final.Dispensed, quantity+1)
				}
				return nil
			},
		},

		// --- the fault, and the one way out of it (issue #6) ------------------
		{
			Name:        "post_while_fault_is_409",
			Targets:     []Target{TargetMock, TargetSimulator},
			Destructive: true,
			Note: "a faulted device refuses new work; before #6 only STATE_DISPENSING " +
				"blocked a POST, so anything that posted cleared a real jam by accident",
			Run: func(c *Ctx) error {
				if err := c.induceFault(); err != nil {
					return err
				}
				got, err := c.postJSON(fmt.Sprintf(`{"tx_id":%q,"quantity":1}`, c.NextTxID("er")), true)
				if err != nil {
					return err
				}
				if err := wantStatus(got, 409); err != nil {
					return err
				}
				if !strings.Contains(got.Body, `"fault"`) {
					return fmt.Errorf("the 409 does not name the fault: %s",
						strings.TrimSpace(got.Body))
				}
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.State != "fault" || health.Fault == "none" || health.Fault == "" {
					return fmt.Errorf("device reports state=%q fault=%q while it is refusing "+
						"work; the terminal has nothing to show the member", health.State, health.Fault)
				}
				return nil
			},
		},
		{
			Name:             "reboot_clears_fault",
			Targets:          []Target{TargetSimulator, TargetHopper},
			NeedsInteraction: true,
			Destructive:      true,
			Note: "the only way out of a fault, by owner decision: clear the jam, refill " +
				"if empty, pull the plug for 5 s",
			Run: func(c *Ctx) error {
				health, res := c.Client.Health()
				if health == nil {
					return fmt.Errorf("no health document: %v", res.Error)
				}
				if health.Fault == "none" {
					return fmt.Errorf("the device is not faulted; run this case after " +
						"post_while_fault_is_409")
				}
				c.Prompt("clear the jam, then power-cycle the dispenser (5 s off)")
				time.Sleep(10 * time.Second)
				after, res := c.Client.Health()
				if after == nil {
					return fmt.Errorf("no health document after the power cycle: %v", res.Error)
				}
				if after.Fault != "none" || after.State == "fault" {
					return fmt.Errorf("after the power cycle the device still reports "+
						"state=%q fault=%q", after.State, after.Fault)
				}
				txID := c.NextTxID("rb")
				if _, res := c.Client.Dispense(txID, 1); res.Error != nil {
					return fmt.Errorf("the device refuses work after the power cycle: %v", res.Error)
				}
				_, _ = c.waitForFinalState(txID, 30*time.Second)
				return nil
			},
		},

		// --- cases that need a human or a switch ------------------------------
		{
			Name:             "reset_mid_dispense_reports_partial_count",
			Targets:          []Target{TargetSimulator, TargetHopper},
			NeedsInteraction: true,
			Note:             "the RST button is a reset the RTC memory survives — the count must be exact",
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
				if tx.CountReliable == nil || !*tx.CountReliable {
					return fmt.Errorf("RST keeps the RTC domain alive, so the count must be reported as exact")
				}
				return nil
			},
		},
		{
			Name:             "power_loss_mid_dispense_reports_count_unreliable",
			Targets:          []Target{TargetSimulator, TargetHopper},
			NeedsInteraction: true,
			Destructive:      true,
			Note:             "the other reset: without the RTC block the count is a lower bound and says so",
			Run: func(c *Ctx) error {
				txID := c.NextTxID("pl")
				if _, res := c.Client.Dispense(txID, 5); res.Error != nil {
					return fmt.Errorf("POST failed: %v", res.Error)
				}
				c.Prompt("cut the power to the dispenser now, while tokens are dropping, " +
					"then switch it back on")
				time.Sleep(10 * time.Second)
				tx, res := c.Client.Status(txID)
				if res.Error != nil {
					return fmt.Errorf("status after the power loss: %v", res.Error)
				}
				if tx.State != "error" {
					return fmt.Errorf("state after a power loss mid-dispense is %q, expected error", tx.State)
				}
				if tx.CountReliable == nil {
					return fmt.Errorf("the transaction response has no count_reliable field")
				}
				if *tx.CountReliable {
					return fmt.Errorf("count_reliable=true after a power loss: " +
						"RTC memory cannot have survived it")
				}
				return nil
			},
		},
	}
}

// The two quantities the mock maps to a reset mid-dispense.  They are an
// implementation detail of the mock (dispenser-mock/scenarios.go); on a real
// device the same two cases are the interactive ones a human triggers with the
// RST button and the power switch.
const (
	crashQuantity     = 5
	powerLossQuantity = 17
	// The mock's overrun scenario: quantity tokens, then one that falls while
	// the disc coasts.  On a real device the same case is the interactive
	// coast_pulse_is_counted_as_an_overrun, where the simulator's 'c' command
	// produces that token.
	overrunQuantity = 18
	// The mock's hopper error: COIN_STUCK (code 1).  It faults the device, so
	// the case that uses it is Destructive and runs last.
	hopperErrorQuantity = 8
)

// induceFault puts the target into a device fault — the state only a power
// cycle ends.  The mock maps quantity 8 to COIN_STUCK; on the simulator the
// operator sets the error switch (docs/hopper-simulator.md).
func (c *Ctx) induceFault() error {
	switch c.Target {
	case TargetMock:
		if health, _ := c.Client.Health(); health != nil && health.Fault != "" && health.Fault != "none" {
			return nil // already faulted; a second POST would only be refused
		}
		txID := c.NextTxID("hw")
		if _, res := c.Client.Dispense(txID, hopperErrorQuantity); res.Error != nil {
			return fmt.Errorf("could not trigger the mock's COIN_STUCK scenario: %v", res.Error)
		}
		deadline := time.Now().Add(5 * time.Second)
		for time.Now().Before(deadline) {
			health, _ := c.Client.Health()
			if health != nil && health.Fault != "" && health.Fault != "none" {
				return nil
			}
			time.Sleep(100 * time.Millisecond)
		}
		return fmt.Errorf("mock did not report a fault after the COIN_STUCK scenario")
	case TargetSimulator:
		if !c.Interactive {
			return fmt.Errorf("set the simulator's error switch first; run with --interactive")
		}
		c.Prompt("set the hopper simulator's ERROR switch to code 1 (COIN_STUCK)")
		return nil
	default:
		return fmt.Errorf("no way to induce a fault on target %s", c.Target)
	}
}
