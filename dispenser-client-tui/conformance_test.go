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

	got := findCase(t, report, "health_protocol_is_2")
	if got.Status != "fail" {
		t.Errorf("health_protocol_is_2 = %s, want fail against protocol 1", got.Status)
	}
}

func TestSuiteCatchesMissingAuthentication(t *testing.T) {
	dev := newFakeDevice("k")
	dev.ignoreAuth = true // a device that serves anyone
	srv := dev.server()
	defer srv.Close()

	report := RunCases(newCtx(srv.URL, "k", TargetMock), ConformanceCases(), "without_api_key")

	for _, name := range []string{"post_without_api_key_is_401", "get_status_without_api_key_is_401"} {
		if got := findCase(t, report, name); got.Status != "fail" {
			t.Errorf("%s = %s, want fail against a device that ignores the key", name, got.Status)
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
	if lastRan != "post_while_error_is_409" {
		t.Errorf("last executed case is %q, want the destructive post_while_error_is_409", lastRan)
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
		if c.Name == "post_while_error_is_409" {
			hardwareError = c
		}
	}
	if hardwareError.Name == "" {
		t.Fatal("post_while_error_is_409 is missing from the table")
	}
	if hardwareError.appliesTo(TargetHopper) {
		t.Error("the hardware-error case cannot be provoked on a real hopper; it must not claim that target")
	}
	if !hardwareError.appliesTo(TargetMock) || !hardwareError.appliesTo(TargetSimulator) {
		t.Error("the hardware-error case must apply to mock and simulator")
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
