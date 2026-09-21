package main

import (
	"bufio"
	"io"
	"strings"
	"testing"
	"time"
)

func newCtx(baseURL, apiKey string, target Target) *Ctx {
	return &Ctx{
		Client: NewDispenserClient(baseURL, apiKey, 5*time.Second),
		Target: target,
		in:     bufio.NewReader(strings.NewReader("")),
		out:    io.Discard,
	}
}

func findCase(t *testing.T, r Report, name string) Result {
	t.Helper()
	for _, c := range r.Cases {
		if c.Name == name {
			return c
		}
	}
	t.Fatalf("case %q not in report", name)
	return Result{}
}

// The suite must be green against a device that honours the protocol.
// If it is not, the failure is in the suite, not in a dispenser.
func TestSuiteIsGreenAgainstConformingDevice(t *testing.T) {
	dev := newFakeDevice("k")
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "")

	if report.Failed != 0 {
		for _, c := range report.Cases {
			if c.Status == "fail" {
				t.Errorf("case %s failed: %s", c.Name, c.Detail)
			}
		}
	}
	if report.Passed == 0 {
		t.Fatal("no case ran")
	}
}

// …and red against the deviations this epic is about. The case carries no
// Note any more: #2 fixed the firmware, so it is no longer known-red anywhere.
func TestSuiteCatchesActiveRetryRejection(t *testing.T) {
	dev := newFakeDevice("k")
	dev.rejectRetry = true // the #2 bug, as the firmware had it
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "")

	got := findCase(t, report, "post_retry_while_dispensing_is_200")
	if got.Status != "fail" {
		t.Errorf("post_retry_while_dispensing_is_200 = %s, want fail against a device that answers 409", got.Status)
	}
}

func TestSuiteCatchesReusedTxIDAcceptance(t *testing.T) {
	dev := newFakeDevice("k")
	dev.acceptReusedQty = true // a device that answers the old quantity as if it matched
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "different_quantity")

	got := findCase(t, report, "post_same_id_different_quantity_is_409")
	if got.Status != "fail" {
		t.Errorf("post_same_id_different_quantity_is_409 = %s, want fail against a device that accepts it", got.Status)
	}
}

func TestSuiteCatchesOrphanedActiveTransaction(t *testing.T) {
	dev := newFakeDevice("k")
	dev.orphanOnReplay = true // the second half of #2, as the firmware had it
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "does_not_orphan_active")

	got := findCase(t, report, "replay_old_tx_during_dispense_does_not_orphan_active")
	if got.Status != "fail" {
		t.Errorf("replay_old_tx_during_dispense_does_not_orphan_active = %s, want fail against a device that orphans it", got.Status)
	}
}

// --- issue #3: the count and the history across a reset ---------------------

func TestSuiteCatchesMissingCountReliable(t *testing.T) {
	dev := newFakeDevice("k")
	dev.omitCountReliable = true // a device that leaves the required field out
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "count_reliable_is_present")

	got := findCase(t, report, "count_reliable_is_present_on_every_transaction")
	if got.Status != "fail" {
		t.Errorf("count_reliable_is_present_on_every_transaction = %s, want fail against a device without the field", got.Status)
	}
}

func TestSuiteCatchesForgottenCrashedTransaction(t *testing.T) {
	dev := newFakeDevice("k")
	dev.forgetCrashedTx = true // the firmware before #3: a reboot loses it
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "crashed_tx_is_found_after_reboot")

	got := findCase(t, report, "crashed_tx_is_found_after_reboot")
	if got.Status != "fail" {
		t.Errorf("crashed_tx_is_found_after_reboot = %s, want fail against a device that forgets it", got.Status)
	}
}

func TestSuiteCatchesFabricatedCountAfterPowerLoss(t *testing.T) {
	dev := newFakeDevice("k")
	dev.claimCountExact = true // a device that calls a lost count exact
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "power_loss_reports_count_unreliable")

	got := findCase(t, report, "power_loss_reports_count_unreliable")
	if got.Status != "fail" {
		t.Errorf("power_loss_reports_count_unreliable = %s, want fail against a device that claims the count is exact", got.Status)
	}
}

func TestSuiteCatchesProtocolMismatch(t *testing.T) {
	dev := newFakeDevice("k")
	dev.protocol = 1 // a device that never got the handshake
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "health_protocol")

	got := findCase(t, report, "health_protocol_is_3")
	if got.Status != "fail" {
		t.Errorf("health_protocol_is_3 = %s, want fail against protocol 1", got.Status)
	}
}

func TestSuiteCatchesMissingAuthentication(t *testing.T) {
	dev := newFakeDevice("k")
	dev.ignoreAuth = true // a device that serves anyone
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "without_a_signature")

	for _, name := range []string{"post_without_a_signature_is_401", "get_status_without_a_signature_is_401"} {
		if got := findCase(t, report, name); got.Status != "fail" {
			t.Errorf("%s = %s, want fail against a device that serves anyone", name, got.Status)
		}
	}
}

// A hardware error cannot be cleared without a power cycle, so a case that
// provokes one must run after everything else — otherwise every later case
// fails for the wrong reason.
func TestDestructiveCasesRunLast(t *testing.T) {
	dev := newFakeDevice("k")
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "")

	lastRan := ""
	for _, c := range report.Cases {
		if c.Status != "skip" {
			lastRan = c.Name
		}
	}
	if lastRan != "post_while_fault_is_409" {
		t.Errorf("last executed case is %q, want the destructive post_while_fault_is_409", lastRan)
	}
}

func TestInteractiveCasesAreSkippedByDefault(t *testing.T) {
	dev := newFakeDevice("k")
	srv := dev.server()
	defer srv.Close()

	ctx := newCtx(srv.URL, "k", TargetSimulator)
	report := RunCases(ctx, ConformanceCases(), "reset_mid_dispense")

	got := findCase(t, report, "reset_mid_dispense_reports_partial_count")
	if got.Status != "skip" {
		t.Errorf("interactive case ran without --interactive: %s (%s)", got.Status, got.Detail)
	}
}

func TestTargetScoping(t *testing.T) {
	cases := ConformanceCases()
	var hardwareError Case
	for _, c := range cases {
		if c.Name == "post_while_fault_is_409" {
			hardwareError = c
		}
	}
	if hardwareError.Name == "" {
		t.Fatal("post_while_fault_is_409 is missing from the table")
	}
	if hardwareError.appliesTo(TargetHopper) {
		t.Error("the fault case cannot be provoked on a real hopper; it must not claim that target")
	}
	if !hardwareError.appliesTo(TargetMock) || !hardwareError.appliesTo(TargetSimulator) {
		t.Error("the fault case must apply to mock and simulator")
	}
}

func TestCheckProtocol(t *testing.T) {
	if err := CheckProtocol(ProtocolVersion); err != nil {
		t.Errorf("current version rejected: %v", err)
	}
	if err := CheckProtocol(0); err == nil {
		t.Error("a device without a protocol field must be refused")
	}
	if err := CheckProtocol(ProtocolVersion + 1); err == nil {
		t.Error("a newer protocol must be refused, not adapted to")
	}
}

func TestNextTxIDFitsProtocolLimit(t *testing.T) {
	ctx := newCtx("http://127.0.0.1:1", "k", TargetMock)
	for i := 0; i < 50; i++ {
		id := ctx.NextTxID("pfx")
		if len(id) == 0 || len(id) > 16 {
			t.Fatalf("tx_id %q has length %d, protocol allows 1-16", id, len(id))
		}
	}
}

func TestSlowQuantityPicksAScenarioPerTarget(t *testing.T) {
	if q := newCtx("http://x", "k", TargetMock).SlowQuantity(); q != 15 {
		t.Errorf("mock slow quantity = %d, want 15 (the mock's 500ms/token scenario)", q)
	}
	if q := newCtx("http://x", "k", TargetHopper).SlowQuantity(); q < 2 || q > 20 {
		t.Errorf("device slow quantity = %d, outside the protocol's 1-20", q)
	}
}

// --- issue #4: the request itself -------------------------------------------

func TestSuiteCatchesUnansweredEmptyBody(t *testing.T) {
	dev := newFakeDevice("k")
	dev.emptyBodyDelay = 2 * time.Second // the empty lambda, as the firmware had it
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "post_without_body")

	got := findCase(t, report, "post_without_body_is_400")
	if got.Status != "fail" {
		t.Errorf("post_without_body_is_400 = %s, want fail against a device that makes the caller wait", got.Status)
	}
}

func TestSuiteCatchesSplitBodyRejection(t *testing.T) {
	dev := newFakeDevice("k")
	dev.truncateBody = 12 // parses the chunk it was handed, ignoring index/total
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "two_segments")

	got := findCase(t, report, "post_body_in_two_segments_is_accepted")
	if got.Status != "fail" {
		t.Errorf("post_body_in_two_segments_is_accepted = %s, want fail against a device that parses one chunk", got.Status)
	}
}

func TestSuiteCatchesSlowPost(t *testing.T) {
	dev := newFakeDevice("k")
	dev.postDelay = 400 * time.Millisecond // flash erase and 500 bytes at 9600 baud
	srv := dev.server()
	defer srv.Close()

	ctx := newCtx(srv.URL, "k", TargetMock)
	ctx.LatencySamples = 5
	report := RunCases(ctx, ConformanceCases(), "post_latency")

	got := findCase(t, report, "post_latency_p95_below_300ms")
	if got.Status != "fail" {
		t.Errorf("post_latency_p95_below_300ms = %s, want fail against a device that works in the callback", got.Status)
	}
}

// --- issue #5: the pulse count, in both directions ---------------------------

func TestSuiteCatchesClampedOverrun(t *testing.T) {
	dev := newFakeDevice("k")
	dev.clampDispensed = true // the firmware before #5: dispensed can never exceed quantity
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "overrun_is_reported")

	got := findCase(t, report, "overrun_is_reported_above_quantity")
	if got.Status != "fail" {
		t.Errorf("overrun_is_reported_above_quantity = %s, want fail against a device that clamps the count", got.Status)
	}
}

func TestSuiteCatchesMissingOverrunMetric(t *testing.T) {
	dev := newFakeDevice("k")
	dev.omitOverrunMetric = true // a device that counts the overrun but never reports it
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "health_reports_overrun")

	got := findCase(t, report, "health_reports_overrun_tokens")
	if got.Status != "fail" {
		t.Errorf("health_reports_overrun_tokens = %s, want fail against a device without the metric", got.Status)
	}
}

func TestSuiteCatchesMissingBodyCap(t *testing.T) {
	dev := newFakeDevice("k")
	dev.noBodyCap = true // a device that takes a body of any size
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "oversized_body")

	got := findCase(t, report, "post_with_oversized_body_is_413")
	if got.Status != "fail" {
		t.Errorf("post_with_oversized_body_is_413 = %s, want fail against a device without the cap", got.Status)
	}
}

// --- issue #6: the fault model ----------------------------------------------
//
// One knob per case, and a test per knob: a case that cannot fail is not a
// case.  The knobs describe the firmware as it was — a device that took a new
// transaction while a jam was active, that had no fault at all, and that
// published an empty sensor it does not have.

func TestSuiteCatchesDispenseWhileFaulted(t *testing.T) {
	dev := newFakeDevice("k")
	dev.acceptWhileFaulted = true // the firmware before #6: only DISPENSING blocked
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "post_while_fault")

	got := findCase(t, report, "post_while_fault_is_409")
	if got.Status != "fail" {
		t.Errorf("post_while_fault_is_409 = %s, want fail against a device that dispenses anyway", got.Status)
	}
}

func TestSuiteCatchesLegacyHealthShape(t *testing.T) {
	dev := newFakeDevice("k")
	dev.legacyHealthShape = true // status + dispenser, no fault
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "health_schema")

	got := findCase(t, report, "health_schema_has_required_fields")
	if got.Status != "fail" {
		t.Errorf("health_schema_has_required_fields = %s, want fail against the protocol-1 shape", got.Status)
	}
}

func TestSuiteCatchesPublishedHopperLow(t *testing.T) {
	dev := newFakeDevice("k")
	dev.publishHopperLow = true // a sensor this hopper does not have
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "hopper_low")

	got := findCase(t, report, "health_has_no_hopper_low")
	if got.Status != "fail" {
		t.Errorf("health_has_no_hopper_low = %s, want fail against a device that still publishes it", got.Status)
	}
}

func TestSuiteCatchesMissingErrorCode(t *testing.T) {
	dev := newFakeDevice("k")
	dev.omitErrorCode = true // one flat "error", as before #6
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "carries_error")

	for _, name := range []string{
		"failed_tx_carries_error_code_and_type",
		"successful_tx_carries_error_type_none",
	} {
		if got := findCase(t, report, name); got.Status != "fail" {
			t.Errorf("%s = %s, want fail against a device without the field", name, got.Status)
		}
	}
}

func TestSuiteCatchesFaultAfterReset(t *testing.T) {
	dev := newFakeDevice("k")
	dev.faultAfterReset = true // one watchdog reset takes the machine out of service
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "crashed_tx_is_found_after_reboot")

	got := findCase(t, report, "crashed_tx_is_found_after_reboot")
	if got.Status != "fail" {
		t.Errorf("crashed_tx_is_found_after_reboot = %s, want fail against a device that faults on a recovered crash", got.Status)
	}
}

func TestSuiteCatchesResetRoute(t *testing.T) {
	dev := newFakeDevice("k")
	dev.servesReset = true // a way out of a fault that is not a power cycle
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "no_reset_route")

	got := findCase(t, report, "no_reset_route_exists")
	if got.Status != "fail" {
		t.Errorf("no_reset_route_exists = %s, want fail against a device with a reset endpoint", got.Status)
	}
}

// --- issue #7: the operational telemetry ------------------------------------

func TestSuiteCatchesMissingOpsTelemetry(t *testing.T) {
	dev := newFakeDevice("k")
	dev.omitOpsTelemetry = true // the health document before #7
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "ops_telemetry")

	got := findCase(t, report, "health_reports_ops_telemetry")
	if got.Status != "fail" {
		t.Errorf("health_reports_ops_telemetry = %s, want fail against a device that "+
			"reports neither heap nor reset reason nor reconnects", got.Status)
	}
}

// --- issue #7: the soak runner ----------------------------------------------
//
// The runner is what Cycle A of the epic executes at the bench
// (`--target simulator --soak 200`).  These tests soak a fake device a few
// dozen times instead, which takes under a second: what is under test here is
// the RUNNER — that it judges what it claims to judge, and that each of its
// five assertions can actually fail.

func TestSoakIsGreenAgainstConformingDevice(t *testing.T) {
	dev := newFakeDevice("k")
	srv := dev.server()
	defer srv.Close()

	report := RunSoak(newCtx(srv.URL, "k", TargetMock), 5, 5*time.Millisecond)

	if report.Failed != 0 {
		for _, c := range report.Cases {
			if c.Status == "fail" {
				t.Errorf("soak assertion %s failed: %s", c.Name, c.Detail)
			}
		}
	}
	if report.Soak == nil {
		t.Fatal("the report carries no soak summary")
	}
	if report.Soak.TokensDispensed != 5 {
		t.Errorf("tokens dispensed = %d, want 5", report.Soak.TokensDispensed)
	}
	if report.Soak.FailedRequests != 0 {
		t.Errorf("failed requests = %d, want 0", report.Soak.FailedRequests)
	}
}

func TestSoakCatchesAHeapLeak(t *testing.T) {
	dev := newFakeDevice("k")
	dev.leakHeap = true // 2 % per dispense: past the 10 % budget within six
	srv := dev.server()
	defer srv.Close()

	report := RunSoak(newCtx(srv.URL, "k", TargetMock), 10, 5*time.Millisecond)

	got := findCase(t, report, "soak_heap_free_within_10_percent")
	if got.Status != "fail" {
		t.Errorf("soak_heap_free_within_10_percent = %s (%s), want fail against a leaking device",
			got.Status, got.Detail)
	}
}

func TestSoakCatchesAResetMidRun(t *testing.T) {
	dev := newFakeDevice("k")
	dev.resetAfter = 3 // the board comes back with a fresh uptime and another reason
	srv := dev.server()
	defer srv.Close()

	report := RunSoak(newCtx(srv.URL, "k", TargetMock), 8, 5*time.Millisecond)

	for _, name := range []string{"soak_uptime_is_monotonic", "soak_reset_reason_unchanged"} {
		got := findCase(t, report, name)
		if got.Status != "fail" {
			t.Errorf("%s = %s (%s), want fail against a device that reset mid-run",
				name, got.Status, got.Detail)
		}
	}
}

func TestSoakCatchesAFailedRequest(t *testing.T) {
	dev := newFakeDevice("k")
	// A device that refuses a retry is not what this case is about; a device
	// that faults is: every dispense after it is a 409, which is exactly what
	// a soak must not report as success.
	dev.fault, dev.faultCode = "jam", 0
	srv := dev.server()
	defer srv.Close()

	report := RunSoak(newCtx(srv.URL, "k", TargetMock), 3, 5*time.Millisecond)

	got := findCase(t, report, "soak_all_requests_succeeded")
	if got.Status != "fail" {
		t.Errorf("soak_all_requests_succeeded = %s (%s), want fail against a faulted device",
			got.Status, got.Detail)
	}
}

func TestSoakRefusesADeviceWithoutTelemetry(t *testing.T) {
	dev := newFakeDevice("k")
	dev.omitOpsTelemetry = true
	srv := dev.server()
	defer srv.Close()

	report := RunSoak(newCtx(srv.URL, "k", TargetMock), 3, 5*time.Millisecond)

	got := findCase(t, report, "soak_device_answers_health")
	if got.Status != "fail" {
		t.Errorf("soak_device_answers_health = %s, want fail: without heap_free and "+
			"reset_reason the run has nothing to compare against", got.Status)
	}
}

// --- issue #8: the suite's own cases for request signing ---------------------
//
// One test per new CI case, each against a fake device with exactly the defect
// the case is meant to catch.  A case that passes against everything is not a
// case — that rule is why these exist and why they are not optional.

func TestSuiteCatchesADeviceThatStillTakesTheAPIKey(t *testing.T) {
	dev := newFakeDevice("k")
	dev.acceptAPIKey = true // protocol 2's bearer header, still honoured
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "old_api_key")
	got := findCase(t, report, "post_with_the_old_api_key_header_is_401")
	if got.Status != "fail" {
		t.Errorf("post_with_the_old_api_key_header_is_401 = %s, want fail against a device "+
			"that still accepts the header the WLAN can read", got.Status)
	}
}

func TestSuiteCatchesAReplayableNonce(t *testing.T) {
	dev := newFakeDevice("k")
	dev.reusableNonce = true // never spent, so the identical POST works twice
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "replayed_signed_post")
	got := findCase(t, report, "replayed_signed_post_is_401")
	if got.Status != "fail" {
		t.Errorf("replayed_signed_post_is_401 = %s, want fail against a device that "+
			"dispenses twice for one captured request", got.Status)
	}
}

func TestSuiteCatchesAConstantNonce(t *testing.T) {
	dev := newFakeDevice("k")
	dev.constantNonce = true // a "nonce" that is the same every time is a constant
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "nonce_endpoint")
	got := findCase(t, report, "nonce_endpoint_hands_out_a_fresh_nonce")
	if got.Status != "fail" {
		t.Errorf("nonce_endpoint_hands_out_a_fresh_nonce = %s, want fail against a device "+
			"handing out one fixed value", got.Status)
	}
}

func TestSuiteCatchesASignatureThatTravelsBetweenPaths(t *testing.T) {
	dev := newFakeDevice("k")
	dev.unboundSignature = true // method and path left out of the canonical string
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "covers_method_and_path")
	got := findCase(t, report, "signature_covers_method_and_path")
	if got.Status != "fail" {
		t.Errorf("signature_covers_method_and_path = %s, want fail against a device "+
			"that signs only the body", got.Status)
	}
}

func TestSuiteCatchesANonceSpentByAPoll(t *testing.T) {
	dev := newFakeDevice("k")
	dev.spendNonceOnRead = true // every status poll would need its own nonce
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "spent_nonce_still_polls")
	got := findCase(t, report, "a_spent_nonce_still_polls")
	if got.Status != "fail" {
		t.Errorf("a_spent_nonce_still_polls = %s, want fail against a device that spends "+
			"the nonce on a read", got.Status)
	}
}

func TestSuiteCatchesAnOpenHealthDocument(t *testing.T) {
	dev := newFakeDevice("k")
	dev.openHealth = true // SSID, IP, firmware and the billed counts to anyone
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "health_without_a_signature")
	got := findCase(t, report, "health_without_a_signature_is_minimal")
	if got.Status != "fail" {
		t.Errorf("health_without_a_signature_is_minimal = %s, want fail against a device "+
			"that hands the whole document to anybody who asks", got.Status)
	}
}

func TestSuiteCatchesAHealthThatDoesNotSayWhichDocumentItIs(t *testing.T) {
	dev := newFakeDevice("k")
	dev.noAuthenticatedFlag = true
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "health_with")
	got := findCase(t, report, "health_with_a_signature_is_whole")
	if got.Status != "fail" {
		t.Errorf("health_with_a_signature_is_whole = %s, want fail against a device that "+
			"never says whether the document is the reduced one", got.Status)
	}
}

func TestSuiteCatchesA401WithoutAReason(t *testing.T) {
	dev := newFakeDevice("k")
	dev.silentUnauthorized = true // "unauthorized", and nothing a client can act on
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "post_without_a_signature")
	got := findCase(t, report, "post_without_a_signature_is_401")
	if got.Status != "fail" {
		t.Errorf("post_without_a_signature_is_401 = %s, want fail against a 401 that does "+
			"not say whether to retry", got.Status)
	}
}
