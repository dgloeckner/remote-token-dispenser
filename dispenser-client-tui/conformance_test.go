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
