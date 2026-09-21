// test_dispense_manager.cpp
//
// Native unit tests for the PRODUCTION DispenseManager.
//
// There is no copy of the class here: `dispense_manager.cpp` is compiled into
// this binary (see build_src_filter of [env:native] in platformio.ini) and the
// mocks implement the production interfaces IStorage / IHopper.  A bug in the
// production code can therefore turn a test red, and a fix can turn it green —
// which was not true before #1.

#include <unity.h>
#include <string.h>

#include "config.h"
#include "crash_state.h"
#include "dispense_manager.h"
#include "pulse_filter.h"
#include "request_body.h"
#include "wifi_supervisor.h"
#include "mocks/count_memory_mock.h"
#include "mocks/flash_storage_mock.h"
#include "mocks/hopper_control_mock.h"

FlashStorageMock* storage;
HopperControlMock* hopper;
CountMemoryMock* counts;
DispenseManager* manager;

void setUp(void) {
    storage = new FlashStorageMock();
    hopper = new HopperControlMock();
    counts = new CountMemoryMock();
    manager = new DispenseManager(*storage, *hopper, *counts);
    _mock_millis = 0;
}

void tearDown(void) {
    delete manager;
    delete counts;
    delete hopper;
    delete storage;
}

// Boot the firmware again on the same storage and the same RTC memory: a new
// DispenseManager over the mocks that survived, which is exactly what a reset
// is from the manager's point of view.
static void reboot(void) {
    delete manager;
    hopper->setPulseCount(0);
    manager = new DispenseManager(*storage, *hopper, *counts);
    manager->begin();
}


// A POST and the loop() pass that follows it.  Since #4 the request handler
// only fills the slot; the flash commit and the motor start happen in loop(),
// at most 10 ms later.  Every test that wants a RUNNING transaction therefore
// needs both halves, and says so by calling this.
static bool startAndRun(const char* tx_id, uint8_t quantity) {
    bool accepted = manager->startDispense(tx_id, quantity);
    manager->loop();
    return accepted;
}

// The loop() pass that reaches the target, plus the settling window after it
// (issue #5).  Reaching the count is not the end of a transaction: the motor
// is stopped, but the disc coasts and a token that is already past the wheel
// still falls.  The firmware keeps counting for DISPENSE_SETTLING_MS and only
// then reports `done` — so a test that wants a FINISHED transaction advances
// the clock, and says so by calling this.
static void loopUntilDone(void) {
    manager->loop();                          // target reached: motor off, settling
    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();                          // window over: DONE
}

// tx_id for the ring tests: "ring1" … "ring9", without pulling in <stdio.h>.
static void snprintfTxId(char* out, int n) {
    out[0] = 'r'; out[1] = 'i'; out[2] = 'n'; out[3] = 'g';
    out[4] = (char)('0' + n);
    out[5] = '\0';
}

// =============================================================================
// The connectivity supervisor (issue #7)
//
// The rule is one line — down for longer than a minute and not dispensing —
// but every part of it is load-bearing, so every part has a test.  The two SDK
// calls around it (`WiFi.status()`, `ESP.restart()`) stay in the sketch and
// are the one thing here that only hardware can prove.
// =============================================================================

void test_a_connected_link_never_restarts(void) {
    WifiSupervisor wifi;
    wifi.begin(true, 0);

    for (unsigned long t = 0; t < 10UL * 60UL * 1000UL; t += 1000) {
        TEST_ASSERT_FALSE(wifi.update(true, false, t));
    }
    TEST_ASSERT_EQUAL_UINT16(0, wifi.reconnects());
    TEST_ASSERT_TRUE(wifi.connected());
}

void test_a_short_outage_is_ridden_out(void) {
    WifiSupervisor wifi;
    wifi.begin(true, 1000);

    TEST_ASSERT_FALSE(wifi.update(false, false, 1000));
    // Exactly the deadline is not past it: the SDK's own auto-reconnect gets
    // the whole minute, which is what covers an ordinary roam.
    TEST_ASSERT_FALSE(wifi.update(false, false, 1000 + WIFI_RESTART_AFTER_MS));
    TEST_ASSERT_EQUAL_UINT32(WIFI_RESTART_AFTER_MS, wifi.disconnectedFor(1000 + WIFI_RESTART_AFTER_MS));
}

void test_a_minute_down_and_idle_restarts(void) {
    WifiSupervisor wifi;
    wifi.begin(true, 1000);

    TEST_ASSERT_FALSE(wifi.update(false, false, 1000));
    TEST_ASSERT_TRUE(wifi.update(false, false, 1001 + WIFI_RESTART_AFTER_MS));
}

void test_a_failed_join_restarts_like_a_dropped_link(void) {
    // setup() waited its 15 s and never joined.  This is the case that used to
    // end with an HTTP server nobody could reach and no way back but a walk to
    // the boathouse.
    WifiSupervisor wifi;
    wifi.begin(false, 15000);

    TEST_ASSERT_FALSE(wifi.update(false, false, 15000));
    TEST_ASSERT_TRUE(wifi.update(false, false, 15001 + WIFI_RESTART_AFTER_MS));
}

void test_the_motor_outranks_the_supervisor(void) {
    WifiSupervisor wifi;
    wifi.begin(true, 0);
    wifi.update(false, true, 0);

    // An hour down: still no restart while a token may be dropping.  A restart
    // here would bill a partial dispense as a RESET error for a customer who
    // is standing in front of a machine that is otherwise working.
    TEST_ASSERT_FALSE(wifi.update(false, true, 60UL * 60UL * 1000UL));
    // The transaction ends; the clock was never reset, so the pass after it
    // restarts at once.
    TEST_ASSERT_TRUE(wifi.update(false, false, 60UL * 60UL * 1000UL + 10));
}

void test_should_restart_is_the_whole_rule(void) {
    TEST_ASSERT_FALSE(WifiSupervisor::shouldRestart(0, false));
    TEST_ASSERT_FALSE(WifiSupervisor::shouldRestart(WIFI_RESTART_AFTER_MS, false));
    TEST_ASSERT_TRUE(WifiSupervisor::shouldRestart(WIFI_RESTART_AFTER_MS + 1, false));
    TEST_ASSERT_FALSE(WifiSupervisor::shouldRestart(WIFI_RESTART_AFTER_MS + 1, true));
}

void test_a_reconnect_is_counted_and_starts_the_clock_over(void) {
    WifiSupervisor wifi;
    wifi.begin(true, 0);

    wifi.update(false, false, 1000);
    TEST_ASSERT_FALSE(wifi.update(true, false, 20000));   // back within the minute
    TEST_ASSERT_EQUAL_UINT16(1, wifi.reconnects());
    TEST_ASSERT_EQUAL_UINT32(0, wifi.disconnectedFor(20000));

    // A second outage gets its own full minute, not the remainder of the first.
    wifi.update(false, false, 30000);
    TEST_ASSERT_FALSE(wifi.update(false, false, 30000 + WIFI_RESTART_AFTER_MS));
    TEST_ASSERT_TRUE(wifi.update(false, false, 30001 + WIFI_RESTART_AFTER_MS));

    wifi.update(true, false, 200000);
    TEST_ASSERT_EQUAL_UINT16(2, wifi.reconnects());
}

void test_the_outage_clock_survives_the_millis_wraparound(void) {
    // millis() wraps after ~49.7 days.  Unsigned subtraction carries the
    // difference across it; a signed or clamped one would hand the supervisor
    // a 49-day outage and restart a healthy device.
    const unsigned long nearWrap = 0xFFFFFF00UL;
    WifiSupervisor wifi;
    wifi.begin(true, nearWrap);

    wifi.update(false, false, nearWrap);
    TEST_ASSERT_FALSE(wifi.update(false, false, nearWrap + 1000));
    TEST_ASSERT_TRUE(wifi.update(false, false, nearWrap + WIFI_RESTART_AFTER_MS + 1));
}

// =============================================================================
// Crash recovery
// =============================================================================

void test_crash_recovery_increments_crash_counter(void) {
    storage->setPersistedTransaction("tx_crash_001", 5, 2, STATE_DISPENSING);

    manager->begin();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, manager->getCrashes(),
        "Crash recovery should increment crash counter");

    Transaction recovered = manager->getTransaction("tx_crash_001");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, recovered.state,
        "Recovered transaction should be in ERROR state");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, recovered.dispensed,
        "Recovered transaction should preserve dispensed count");
}

void test_no_crash_recovery_does_not_increment_counter(void) {
    manager->begin();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, manager->getCrashes(),
        "No crash recovery means crash counter stays zero");
}

void test_failures_calculation_includes_crashes(void) {
    storage->setPersistedTransaction("tx_crash_002", 3, 1, STATE_DISPENSING);

    manager->begin();

    uint16_t failures = manager->getTotalDispenses()
                      - manager->getSuccessful()
                      - manager->getJams();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        manager->getCrashes(), failures,
        "Failures should equal crashes when no other failure types");
}

void test_crash_recovery_preserves_token_counts(void) {
    storage->setPersistedTransaction("tx_crash", 5, 2, STATE_DISPENSING);

    manager->begin();

    Transaction recovered = manager->getTransaction("tx_crash");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, recovered.dispensed,
        "Crash recovery should preserve dispensed count");
}

void test_persisted_error_state_is_cleared_on_boot(void) {
    // A power cycle is the documented way to clear a jam.
    storage->setPersistedTransaction("tx_jam", 4, 1, STATE_ERROR);

    manager->begin();

    TEST_ASSERT_TRUE_MESSAGE(
        manager->isIdle(),
        "Booting on a persisted ERROR clears it (power cycle = manual reset)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_IDLE, manager->getActiveTransaction().state,
        "Active transaction should be IDLE after clearing a persisted error");
}

// =============================================================================
// Token metrics
// =============================================================================

void test_requested_tokens_accumulates_across_transactions(void) {
    manager->begin();
    startAndRun("tx001", 5);
    startAndRun("tx002", 3);  // busy → rejected

    hopper->setPulseCount(5);
    loopUntilDone();                     // completes tx001

    startAndRun("tx002", 3);  // now accepted

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        8, manager->getRequestedTokens(),
        "Requested tokens should accumulate across all transactions");
}

void test_dispensed_tokens_tracks_actual_dispensed(void) {
    manager->begin();

    startAndRun("tx001", 5);
    hopper->setPulseCount(5);
    loopUntilDone();                     // full success

    startAndRun("tx002", 3);
    hopper->setPulseCount(2);
    hopper->setJamDetected(true);
    manager->loop();                     // jam after 2 of 3

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        7, manager->getDispensedTokens(),
        "Dispensed tokens should count actual tokens dispensed, including partials");
}

// =============================================================================
// Dispense lifecycle
// =============================================================================

void test_start_dispense_persists_and_starts_motor(void) {
    manager->begin();

    TEST_ASSERT_TRUE(startAndRun("tx_start", 3));

    TEST_ASSERT_TRUE_MESSAGE(hopper->isMotorRunning(), "Motor must be running");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        3, hopper->getMotorStopAt(),
        "The loop() pass must arm the ISR stop with the requested quantity");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "The started transaction must be persisted exactly once");
    TEST_ASSERT_FALSE_MESSAGE(manager->isIdle(), "Manager is busy while dispensing");
}

void test_busy_with_other_tx_is_rejected(void) {
    manager->begin();
    startAndRun("tx_a", 3);

    TEST_ASSERT_FALSE_MESSAGE(
        startAndRun("tx_b", 2),
        "A different tx_id while dispensing must be rejected (409 busy)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "A rejected request must not start the motor a second time");
}

void test_jam_stops_motor_and_records_partial(void) {
    manager->begin();
    startAndRun("tx_jam", 4);

    hopper->setPulseCount(1);
    hopper->setJamDetected(true);
    manager->loop();

    TEST_ASSERT_FALSE_MESSAGE(hopper->isMotorRunning(), "A jam must stop the motor");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, manager->getJams(), "Jam counter");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, manager->getPartial(), "Partial counter");

    Transaction tx = manager->getTransaction("tx_jam");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_ERROR, tx.state, "Jammed transaction is ERROR");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx.dispensed, "Partial count is recorded exactly");
}

void test_completed_dispense_leaves_the_device_idle_and_faultless(void) {
    // The replacement for "self-healing" (issue #6).  A dispense can no longer
    // clear anything: a decoded hopper error faults the device, and a fault is
    // ended by a reboot and by nothing else (owner decision 3).  What a clean
    // dispense must do is leave nothing behind.
    manager->begin();
    startAndRun("tx_heal", 1);

    hopper->simulatePulseISR();
    loopUntilDone();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_NONE, manager->getFault(),
        "A dispense that went through leaves no fault");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_IDLE, manager->getDeviceState(),
        "… and the device is idle, not 'done'");
    Transaction tx = manager->getTransaction("tx_heal");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        TX_ERROR_NONE, tx.error_kind,
        "… and the transaction carries no error kind");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "NONE", txErrorTypeToString(tx.error_kind, tx.error_code),
        "… which is the string every transaction response carries");
}


// =============================================================================
// The fault model (issue #6)
//
// Two things used to be conflated: "the last transaction failed" and "this
// machine needs a human".  The device now carries a FAULT of its own, and the
// rules around it are all owner decisions of 2026-09-20:
//
//   - a jam or a decoded hopper error raises it and ends the dispense at once
//   - while it is up, a POST for a NEW transaction is refused (DISPENSE_FAULT
//     → 409 fault); a retry of a known one is still answered
//   - it is cleared by a reboot and by NOTHING else, and it is not persisted:
//     a watchdog reset clears it too, and a jam that is still there simply
//     faults the next dispense again, having dispensed and billed nothing
//   - a transaction recovered after a reset sets NO fault; the device is
//     sellable again without anyone touching it
// =============================================================================

void test_jam_sets_fault_and_blocks_new_dispense(void) {
    manager->begin();
    startAndRun("tx_jf", 4);

    hopper->setPulseCount(1);
    hopper->setJamDetected(true);
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_JAM, manager->getFault(), "A jam timeout raises the device fault");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_FAULT, manager->getDeviceState(),
        "… and that is what /health reports as the device state");
    TEST_ASSERT_FALSE_MESSAGE(
        manager->isIdle(), "A faulted device is not idle, whatever the transaction says");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_FAULT, manager->requestDispense("tx_after", 1),
        "A new transaction while the device is faulted is refused (409 fault)");

    manager->loop();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "… and above all it must not run the motor into the jam");
}

void test_idempotent_get_post_still_answer_while_faulted(void) {
    manager->begin();
    startAndRun("tx_idf", 4);

    hopper->setPulseCount(2);
    hopper->setJamDetected(true);
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_IDEMPOTENT, manager->requestDispense("tx_idf", 4),
        "The retry of the transaction that jammed is still answered with its state: "
        "the terminal is asking what happened, not asking for tokens");

    Transaction tx = manager->getTransaction("tx_idf");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_ERROR, tx.state, "… which is error …");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, tx.dispensed, "… with the exact partial count");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        TX_ERROR_JAM_TIMEOUT, tx.error_kind,
        "… and the reason, so the terminal can tell a jam from a motor fault");
    TEST_ASSERT_EQUAL_STRING("JAM_TIMEOUT",
        txErrorTypeToString(tx.error_kind, tx.error_code));
}

void test_crash_recovery_leaves_device_idle(void) {
    // The field failure this issue is named after: one watchdog reset took the
    // machine out of service, because the recovered transaction left the
    // device reporting "error" and the terminal greyed the tokens out — so
    // nobody could start the dispense that would have cleared it.
    storage->setPersistedTransaction("tx_boot", 5, 0, STATE_DISPENSING);
    counts->survivesWith("tx_boot", 2);

    manager->begin();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_NONE, manager->getFault(),
        "A recovered crash is not a fault: nothing is wrong with the machine");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_IDLE, manager->getDeviceState(), "… so the device is idle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_IDLE, manager->getActiveTransaction().state,
        "… and the recovered transaction does not occupy the active slot");
    TEST_ASSERT_TRUE_MESSAGE(
        startAndRun("tx_sell", 1),
        "The device is sellable again without anyone touching it");

    Transaction recovered = manager->getTransaction("tx_boot");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, recovered.state, "The transaction itself is still an error …");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, recovered.dispensed, "… with the count from RTC memory (issue #3) …");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        TX_ERROR_RESET, recovered.error_kind, "… and RESET as its reason");
    TEST_ASSERT_EQUAL_STRING("RESET",
        txErrorTypeToString(recovered.error_kind, recovered.error_code));
}

void test_hopper_error_during_dispense_aborts_before_jam_timeout(void) {
    manager->begin();
    startAndRun("tx_he", 5);

    hopper->setPulseCount(1);
    manager->loop();

    hopper->reportError(5);   // MOTOR_FAULT on the hopper's error line
    manager->loop();

    TEST_ASSERT_FALSE_MESSAGE(
        hopper->isMotorRunning(),
        "The hopper says the motor is faulty; the firmware must stop driving it "
        "now, not in five seconds when the jam watchdog notices");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, manager->getJams(),
        "It is not a jam — the jam watchdog never fired, the hopper spoke");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_HOPPER_ERROR, manager->getFault(), "The device is faulted …");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        5, manager->getFaultCode(), "… and the code says which error it was");

    Transaction tx = manager->getTransaction("tx_he");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_ERROR, tx.state, "The transaction failed …");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx.dispensed, "… with the token that did fall …");
    TEST_ASSERT_EQUAL_INT_MESSAGE(TX_ERROR_HOPPER, tx.error_kind, "… and the hopper's own reason");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, tx.error_code, "… as a code the terminal can read");
    TEST_ASSERT_EQUAL_STRING("MOTOR_FAULT",
        txErrorTypeToString(tx.error_kind, tx.error_code));
}

void test_hopper_error_while_idle_faults_the_device(void) {
    manager->begin();

    hopper->reportError(3);   // JAM_PERMANENT, with nothing running
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_HOPPER_ERROR, manager->getFault(),
        "An error the hopper reports while idle is still 'this machine needs a human'");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_FAULT, manager->requestDispense("tx_nope", 1),
        "… so the next request is refused instead of driving a jammed hopper");
}

void test_a_fault_in_the_settling_window_leaves_no_settling_behind(void) {
    // The settling window (issue #5) is the one state where the motor is off
    // and the transaction is still running.  A fault that ends it must clear
    // the flag, or the next loop() pass finishes a transaction that failed.
    manager->begin();
    startAndRun("tx_stf", 2);

    hopper->setPulseCount(2);
    manager->loop();          // target reached: motor off, settling

    hopper->reportError(3);
    manager->loop();          // the fault ends it mid-window

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, manager->getTransaction("tx_stf").state,
        "The transaction the fault interrupted is an error");

    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, manager->getTransaction("tx_stf").state,
        "A settling flag left standing would report it done once the window is over");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, manager->getSuccessful(), "… and count it as a success");
}

void test_fault_is_not_persisted_and_boot_starts_idle(void) {
    manager->begin();
    startAndRun("tx_np", 3);
    hopper->setJamDetected(true);
    manager->loop();
    TEST_ASSERT_EQUAL_INT(FAULT_JAM, manager->getFault());

    hopper->setJamDetected(false);   // the jam was cleared by hand
    reboot();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_NONE, manager->getFault(),
        "A boot clears the fault — that is the whole of the reset story, and it "
        "is why the fault is not in the persisted record");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_IDLE, manager->getDeviceState(), "… the device starts idle …");
    TEST_ASSERT_TRUE_MESSAGE(
        startAndRun("tx_np2", 1), "… and takes transactions again");
}

void test_jam_after_reboot_sets_fault_again_with_zero_dispensed(void) {
    // The accepted consequence of owner decision 3: a watchdog reset clears a
    // jam the operator has not cleared.  Nothing is lost by it — the next
    // dispense runs into the same jam, and bills nothing.
    manager->begin();
    startAndRun("tx_j1", 3);
    hopper->setJamDetected(true);
    manager->loop();

    reboot();                        // the jam is still in the hopper
    TEST_ASSERT_EQUAL_INT(FAULT_NONE, manager->getFault());

    TEST_ASSERT_TRUE_MESSAGE(startAndRun("tx_j2", 3),
        "The device accepts the transaction: it cannot know the jam is still there");
    manager->loop();                 // … and runs straight into it

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        FAULT_JAM, manager->getFault(), "The jam faults the device again");
    Transaction tx = manager->getTransaction("tx_j2");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_ERROR, tx.state, "… the transaction fails …");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, tx.dispensed,
        "… and nothing fell, so nothing is billed: the cost of the accepted "
        "consequence is one failed transaction, not a token");
}

void test_device_state_matrix(void) {
    // fault x active transaction, as GET /health reports the pair.
    manager->begin();
    TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_IDLE, manager->getDeviceState(), "idle, no fault");
    TEST_ASSERT_EQUAL_STRING("idle", deviceStateToString(manager->getDeviceState()));
    TEST_ASSERT_EQUAL_STRING("none", faultToString(manager->getFault()));

    startAndRun("tx_mx", 2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_DISPENSING, manager->getDeviceState(), "a transaction is running");

    hopper->setPulseCount(2);
    manager->loop();          // target reached: settling, motor off
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_DISPENSING, manager->getDeviceState(),
        "the settling window is still busy — there is no separate settling state");

    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_IDLE, manager->getDeviceState(), "and idle again when it is over");

    startAndRun("tx_mx2", 2);
    hopper->setJamDetected(true);
    manager->loop();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DEVICE_FAULT, manager->getDeviceState(), "a fault outranks everything");
    TEST_ASSERT_EQUAL_STRING("fault", deviceStateToString(manager->getDeviceState()));
    TEST_ASSERT_EQUAL_STRING("jam", faultToString(manager->getFault()));
}

void test_error_kind_survives_a_reboot(void) {
    manager->begin();
    startAndRun("tx_persist", 3);
    hopper->setPulseCount(1);
    hopper->setJamDetected(true);
    manager->loop();

    hopper->setJamDetected(false);
    reboot();

    Transaction tx = manager->getTransaction("tx_persist");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, tx.state, "The failed transaction is in the persisted ring …");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        TX_ERROR_JAM_TIMEOUT, tx.error_kind,
        "… and so is its reason: the terminal may only ask after the reboot");
}

void test_unknown_tx_is_reported_as_empty(void) {
    manager->begin();

    Transaction tx = manager->getTransaction("nope");

    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_IDLE, tx.state, "Unknown tx reports IDLE …");
    TEST_ASSERT_EQUAL_MESSAGE('\0', tx.tx_id[0],
        "… with an empty tx_id — that pair is what the HTTP layer turns into 404");
}

// =============================================================================
// Idempotency (dispenser-protocol.md § Design Principles 1)
// =============================================================================

void test_replay_of_finished_tx_returns_cached_result(void) {
    manager->begin();
    startAndRun("tx_done", 2);
    hopper->setPulseCount(2);
    loopUntilDone();                     // tx_done is DONE and in the ring

    TEST_ASSERT_TRUE_MESSAGE(
        startAndRun("tx_done", 2),
        "Replaying a finished tx_id is not an error");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "A replay must not dispense again");

    Transaction tx = manager->getTransaction("tx_done");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DONE, tx.state, "Cached state is returned");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, tx.dispensed, "Cached count is returned");
}

// ---------------------------------------------------------------------------
// Idempotency of the ACTIVE transaction (issue #2).
//
// dispenser-protocol.md § POST /dispense: a POST for the transaction that is
// currently running returns its current state (200), and an idempotent hit
// never touches the active transaction.  Both used to be wrong — the retry was
// answered 409 busy and the hit overwrote active_tx, which switched the jam
// watchdog off while the motor was running.
// ---------------------------------------------------------------------------
void test_retry_of_active_tx_returns_true_and_does_not_restart(void) {
    manager->begin();
    startAndRun("tx_active", 5);

    bool retry = startAndRun("tx_active", 5);

    TEST_ASSERT_TRUE_MESSAGE(
        retry,
        "A retry of the RUNNING transaction is its own state, not 409 busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "The retry must never start the motor twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getResetPulseCalls(),
        "A retry must never reset the pulse counter of a running dispense");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "A retry must not write the transaction to flash a second time");

    Transaction active = manager->getActiveTransaction();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("tx_active", active.tx_id,
        "The running transaction stays the active one");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DISPENSING, active.state,
        "… and stays in DISPENSING, so loop() keeps watching the motor");
}

void test_idempotent_hit_does_not_touch_active_tx(void) {
    manager->begin();

    startAndRun("tx_old", 1);
    hopper->simulatePulseISR();
    loopUntilDone();                      // tx_old DONE, lands in the ring

    startAndRun("tx_new", 5);  // now dispensing
    startAndRun("tx_old", 1);  // idempotent hit for the finished one

    Transaction active = manager->getActiveTransaction();
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "tx_new", active.tx_id,
        "The idempotent hit must not replace the running transaction");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DISPENSING, active.state,
        "… and must not flip it out of DISPENSING");

    // The point of the assertions above: the watchdog is still on the motor.
    hopper->setPulseCount(2);
    hopper->setJamDetected(true);
    manager->loop();

    TEST_ASSERT_FALSE_MESSAGE(hopper->isMotorRunning(),
        "A jam after an idempotent hit must still stop the motor");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, manager->getJams(), "… and be counted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, manager->getTransaction("tx_new").state,
        "… and be recorded against the running transaction");
}

void test_idempotent_hit_while_idle_leaves_active_idle(void) {
    // The second half of #2 seen through GET /health: the health document
    // reports getActiveTransaction().state.  A replay of a finished tx_id
    // must not make an idle device claim it is doing something.
    manager->begin();
    startAndRun("tx_done", 1);
    hopper->simulatePulseISR();
    loopUntilDone();                      // idle again, tx_done in the ring

    startAndRun("tx_done", 1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_IDLE, manager->getActiveTransaction().state,
        "A replay while idle leaves the device idle");
    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(), "… and ready for the next transaction");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DONE, manager->getTransaction("tx_done").state,
        "… while the replayed transaction still answers with its cached state");
}

void test_outcome_distinguishes_busy_from_a_reused_tx_id(void) {
    // The HTTP layer needs the reason, not just "no": 409 busy means ANOTHER
    // transaction, 409 tx_id reused means the caller contradicted itself.
    manager->begin();
    startAndRun("tx_run", 3);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_BUSY, manager->requestDispense("tx_other", 1),
        "A different tx_id while dispensing is busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_TX_ID_REUSED, manager->requestDispense("tx_run", 4),
        "The ACTIVE tx_id with another quantity is a reused id, not busy");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_IDEMPOTENT, manager->requestDispense("tx_run", 3),
        "The active tx_id with its own quantity is the idempotent retry");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "None of the three answers may start a second dispense");
}

void test_same_tx_id_different_quantity_is_rejected(void) {
    manager->begin();
    startAndRun("tx_qty", 2);
    hopper->simulatePulseISR();
    hopper->simulatePulseISR();
    loopUntilDone();                      // tx_qty DONE with quantity 2

    TEST_ASSERT_FALSE_MESSAGE(
        startAndRun("tx_qty", 5),
        "The same tx_id with another quantity is a client bug, not a retry");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "… and must not dispense anything");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, manager->getTransaction("tx_qty").quantity,
        "… and must not silently answer with the old quantity as if it matched");
}

// =============================================================================
// The request slot (issue #4)
//
// ESPAsyncWebServer runs the POST handler from the TCP/SDK context, not from
// loop().  Doing the flash commit and the motor start there blocks the WiFi
// stack for the duration of a sector erase plus ~500 bytes of serial output —
// the most plausible source of the multi-second POSTs the terminal retries.
//
// So requestDispense() only DECIDES (in memory, no I/O) and parks the accepted
// request in a slot; the next loop() pass commits it and starts the motor.
// Both halves are pinned here: the callback must touch nothing, and the slot
// must be consumed exactly once.
// =============================================================================

void test_accepted_request_touches_neither_flash_nor_motor(void) {
    manager->begin();
    storage->resetCallCounts();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_STARTED, manager->requestDispense("tx_slot", 3),
        "A new transaction on an idle device is accepted");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, storage->getSaveCalls(),
        "No flash commit may happen in the async callback");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, hopper->getStartMotorCalls(),
        "No motor start may happen in the async callback");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, counts->getWriteCalls(),
        "… and no RTC write either");

    // The POST still answers from in-memory state, immediately.
    Transaction answered = manager->getTransaction("tx_slot");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DISPENSING, answered.state,
        "The POST answers 'dispensing' straight away, from RAM");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, answered.dispensed, "… with dispensed 0");
}

void test_pending_request_is_started_by_loop_exactly_once(void) {
    manager->begin();
    storage->resetCallCounts();

    manager->requestDispense("tx_slot2", 4);
    manager->loop();

    TEST_ASSERT_TRUE_MESSAGE(hopper->isMotorRunning(),
        "loop() picks the slot up and starts the motor");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        4, hopper->getMotorStopAt(),
        "… with the ISR stop armed at the requested quantity");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "… and exactly one commit, on the loop side");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, counts->storedCount(),
        "… and the live count seeded to zero");

    manager->loop();
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "The slot is consumed once: further passes must not restart the motor");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "… nor commit a second time");
}

void test_second_request_before_loop_is_busy(void) {
    // The window the slot opens: two POSTs can land in the same async batch,
    // before loop() has run at all.  The second one must still be a 409.
    manager->begin();
    manager->requestDispense("tx_first", 2);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_BUSY, manager->requestDispense("tx_second", 1),
        "A second transaction while one is pending must be rejected");

    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "Only the accepted transaction may reach the motor");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "tx_first", manager->getActiveTransaction().tx_id,
        "… and it is the first one");
}

void test_retry_before_loop_does_not_queue_a_second_start(void) {
    // The terminal's 3 s timeout can fire before the device has run loop()
    // once.  That retry is the same transaction, not a second one.
    manager->begin();
    manager->requestDispense("tx_retry", 2);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_IDEMPOTENT, manager->requestDispense("tx_retry", 2),
        "A retry of the pending transaction is idempotent");

    manager->loop();
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "A retry must not put a second start into the slot");
}

// =============================================================================
// The request body (issue #4)
//
// ESPAsyncWebServer hands the body callback one chunk at a time.  The POST
// handler used to parse whatever chunk it got, so a body split across two TCP
// segments was answered "400 invalid json" — at random, since the split
// depends on the network and not on the request.
//
// These tests live in the same binary because PlatformIO builds one test
// binary per directory; there can be only one main().
// =============================================================================

static BodyStatus feed(RequestBody& body, const char* chunk, size_t index, size_t total) {
    return body.append((const uint8_t*)chunk, strlen(chunk), index, total);
}

void test_body_in_one_chunk_is_complete(void) {
    RequestBody body;
    body.reset();
    const char* json = "{\"tx_id\":\"a1\",\"quantity\":2}";

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_COMPLETE, feed(body, json, 0, strlen(json)),
        "A body that arrives in one piece is complete at once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(json, body.data(), "… and is the body that was sent");
}

void test_body_in_two_chunks_is_assembled_before_parsing(void) {
    RequestBody body;
    body.reset();
    const char* first = "{\"tx_id\":\"a1\",";
    const char* second = "\"quantity\":2}";
    size_t total = strlen(first) + strlen(second);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_INCOMPLETE, feed(body, first, 0, total),
        "Half a body is not a body: nothing may be parsed yet");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_COMPLETE, feed(body, second, strlen(first), total),
        "The last chunk completes it");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "{\"tx_id\":\"a1\",\"quantity\":2}", body.data(),
        "… and the two segments are one body again");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(total, (uint32_t)body.size(), "… of the announced length");
}

void test_empty_body_is_complete_and_empty(void) {
    RequestBody body;
    body.reset();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_COMPLETE, body.append(NULL, 0, 0, 0),
        "A zero-length body is complete, not pending");
    TEST_ASSERT_TRUE_MESSAGE(body.isEmpty(), "… and empty, which the handler answers with 400");
}

void test_body_over_the_cap_is_refused_not_truncated(void) {
    RequestBody body;
    body.reset();
    char big[REQUEST_BODY_CAPACITY + 8];
    memset(big, 'x', sizeof(big));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_TOO_LARGE, body.append((const uint8_t*)big, 8, 0, sizeof(big)),
        "An oversized body is refused from its first chunk — 413, not a parse error");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_TOO_LARGE, body.append((const uint8_t*)big, 8, 8, sizeof(big)),
        "… and the verdict stands for the rest of the stream");
}

void test_chunk_with_a_gap_is_malformed(void) {
    RequestBody body;
    body.reset();
    const char* first = "{\"tx_id\":";

    feed(body, first, 0, 32);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        BODY_MALFORMED, feed(body, "\"a1\"}", strlen(first) + 4, 32),
        "A chunk that does not continue where the last one ended is not guessed at");
}

// =============================================================================
// Immediate motor stop (double-dispense regression, commit a9f15af)
// =============================================================================

void test_motor_stops_immediately_on_isr_pulse_without_loop(void) {
    manager->begin();
    startAndRun("tx_isr002", 1);
    TEST_ASSERT_TRUE_MESSAGE(hopper->isMotorRunning(), "Motor runs after the accepting loop() pass");

    hopper->simulatePulseISR();   // no loop() call in between

    TEST_ASSERT_FALSE_MESSAGE(
        hopper->isMotorRunning(),
        "Motor must stop at ISR level, not wait for the next loop() iteration");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, hopper->getMotorStopAt(),
        "ISR stop target must be cleared once the motor is stopped");
}

void test_loop_completes_transaction_after_isr_stop(void) {
    manager->begin();
    startAndRun("tx_isr004", 1);

    hopper->simulatePulseISR();
    loopUntilDone();

    Transaction tx = manager->getTransaction("tx_isr004");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DONE, tx.state,
        "Transaction should be STATE_DONE after ISR stop + loop()");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx.dispensed, "Dispensed count should be 1");
    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(), "Manager is idle again");
}


// =============================================================================
// Count and history survive a reset (issue #3)
//
// Two different resets, and the difference is the whole point:
//   - watchdog / exception / brownout: RTC memory survives, the count is exact
//   - power loss:                      RTC memory is gone, the count is a
//                                      lower bound and says so
// =============================================================================

void test_reset_mid_dispense_recovers_rtc_count(void) {
    manager->begin();
    startAndRun("tx_rst", 5);

    // Three tokens are in the tray when the watchdog fires.
    hopper->setPulseCount(3);
    manager->loop();

    reboot();  // RTC memory survives this kind of reset

    Transaction recovered = manager->getTransaction("tx_rst");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, recovered.state,
        "A reset mid-dispense ends the transaction in ERROR");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        3, recovered.dispensed,
        "The three tokens in the tray must be billed, not reported as zero");
    TEST_ASSERT_TRUE_MESSAGE(
        recovered.count_reliable,
        "The RTC block survived, so the count is exact");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        3, manager->getDispensedTokens(),
        "… and the token metric counts them too");
}

void test_power_loss_mid_dispense_reports_count_unreliable(void) {
    manager->begin();
    startAndRun("tx_pwr", 5);

    hopper->setPulseCount(3);
    manager->loop();

    counts->loseRtcMemory();  // the supply went away, RTC memory with it
    reboot();

    Transaction recovered = manager->getTransaction("tx_pwr");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, recovered.state,
        "A power loss mid-dispense also ends in ERROR");
    TEST_ASSERT_FALSE_MESSAGE(
        recovered.count_reliable,
        "Without the RTC block the count is a lower bound, and must say so");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, recovered.dispensed,
        "The lower bound is what flash holds — here the zero written at start");
}

void test_live_count_goes_to_rtc_and_not_to_flash(void) {
    manager->begin();
    storage->resetCallCounts();

    startAndRun("tx_live", 3);
    hopper->setPulseCount(1);
    manager->loop();
    hopper->setPulseCount(2);
    manager->loop();

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, counts->storedCount(),
        "Every token updates the live count in RTC memory");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "… and none of them costs a flash commit (owner decision, 2026-09-20)");
}

void test_done_tx_is_found_after_reboot(void) {
    manager->begin();
    startAndRun("tx_keep", 2);
    hopper->setPulseCount(2);
    loopUntilDone();                     // DONE

    reboot();

    Transaction found = manager->getTransaction("tx_keep");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DONE, found.state,
        "A finished transaction must survive a reboot: a 404 would be "
        "indistinguishable from 'the request never arrived'");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, found.dispensed, "… with its count");
    TEST_ASSERT_TRUE_MESSAGE(found.count_reliable, "… and its count_reliable flag");
}

void test_ring_survives_reboot_and_wraps_at_8(void) {
    manager->begin();

    char id[8];
    for (int i = 1; i <= 9; i++) {
        snprintfTxId(id, i);
        startAndRun(id, 1);
        hopper->setPulseCount(1);
        loopUntilDone();
    }

    reboot();

    snprintfTxId(id, 1);
    Transaction evicted = manager->getTransaction(id);
    TEST_ASSERT_EQUAL_MESSAGE('\0', evicted.tx_id[0],
        "The ring holds 8; the ninth transaction pushes the first one out");

    for (int i = 2; i <= 9; i++) {
        snprintfTxId(id, i);
        Transaction kept = manager->getTransaction(id);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            STATE_DONE, kept.state,
            "The last eight transactions must come back after a reboot");
    }
}

void test_second_boot_after_crash_adds_no_empty_history_entry(void) {
    storage->setPersistedTransaction("tx_two", 4, 0, STATE_DISPENSING);
    counts->survivesWith("tx_two", 2);

    manager->begin();   // first boot: crash recovery
    reboot();           // second boot: the persisted ERROR is cleared

    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(),
        "The second boot clears the error, as a power cycle always did");

    PersistedRecord record = storage->raw();
    int used = 0;
    for (int i = 0; i < PERSIST_RING_SIZE; i++) {
        if (record.ring[i].tx_id[0] != '\0') {
            used++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, used,
        "The second boot must not plant an empty entry in the ring");

    Transaction recovered = manager->getTransaction("tx_two");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, recovered.state,
        "… and clearing the error must not throw the crashed transaction away");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, recovered.dispensed, "… nor its recovered count");
}

void test_successful_dispense_commits_twice(void) {
    manager->begin();
    storage->resetCallCounts();

    startAndRun("tx_cost", 2);
    hopper->setPulseCount(2);
    loopUntilDone();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        2, storage->getSaveCalls(),
        "A successful dispense costs two commits: start and finish");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, storage->getClearCalls(),
        "… and no third erase — the ring IS the record, it is not cleared");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, counts->getInvalidateCalls(),
        "The finished transaction releases the RTC block");
}

void test_corrupt_record_is_ignored(void) {
    manager->begin();
    startAndRun("tx_bad", 2);
    hopper->setPulseCount(2);
    loopUntilDone();

    storage->corruptOneByte();
    reboot();

    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(),
        "A record that fails its checksum is empty, never data");
    TEST_ASSERT_EQUAL_MESSAGE('\0', manager->getTransaction("tx_bad").tx_id[0],
        "… so nothing from it is answered as a transaction");
}

void test_old_layout_version_is_ignored(void) {
    manager->begin();
    startAndRun("tx_old_v", 2);
    hopper->setPulseCount(2);
    loopUntilDone();

    storage->setStoredLayoutVersion(PERSIST_LAYOUT_VERSION - 1);
    reboot();

    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(),
        "Bytes from another layout are ignored, not reinterpreted");
}

void test_rtc_block_of_another_tx_is_not_this_count(void) {
    // The block belongs to the transaction that wrote it.  Reading a stale one
    // would bill the previous customer's tokens to this transaction.
    storage->setPersistedTransaction("tx_mine", 5, 0, STATE_DISPENSING);
    counts->survivesWith("tx_someone_else", 4);

    manager->begin();

    Transaction recovered = manager->getTransaction("tx_mine");
    TEST_ASSERT_FALSE_MESSAGE(
        recovered.count_reliable,
        "A block from another tx_id is no count for this one");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, recovered.dispensed, "… and must not be adopted as the count");
}

// =============================================================================
// The guards on the two blocks themselves
// =============================================================================

void test_sealed_record_validates_and_zeroed_one_does_not(void) {
    PersistedRecord record;
    memset(&record, 0, sizeof(record));
    TEST_ASSERT_FALSE_MESSAGE(persistedRecordValid(record),
        "A zeroed EEPROM is not a record");

    sealPersistedRecord(record);
    TEST_ASSERT_TRUE_MESSAGE(persistedRecordValid(record),
        "A sealed record validates");

    record.active.dispensed++;
    TEST_ASSERT_FALSE_MESSAGE(persistedRecordValid(record),
        "A byte changed after sealing breaks the checksum");
}

void test_sealed_rtc_block_validates_and_zeroed_one_does_not(void) {
    RtcCountBlock block;
    memset(&block, 0, sizeof(block));
    TEST_ASSERT_FALSE_MESSAGE(rtcCountBlockValid(block),
        "RTC memory after a power loss is not a count");

    strncpy(block.tx_id, "tx_seal", sizeof(block.tx_id) - 1);
    block.dispensed = 3;
    sealRtcCountBlock(block);
    TEST_ASSERT_TRUE_MESSAGE(rtcCountBlockValid(block), "A sealed block validates");

    block.dispensed = 4;
    TEST_ASSERT_FALSE_MESSAGE(rtcCountBlockValid(block),
        "A block damaged by a reset mid-write is rejected");
}


// =============================================================================
// The pulse filter (issue #5)
//
// The coin ISR counted raw falling edges.  A bouncing optocoupler or an EMI
// spike from the motor on the same supply is an edge too, and each one counted
// as a token: the ISR stop fired early and the member got fewer tokens than
// the terminal billed.  The table below is the hopper's own signal, in
// microseconds — 30 ms pulses roughly a second apart — plus the noise the
// simulator's bounce mode produces.
// =============================================================================

// The production spacing, in microseconds.
static uint32_t minGapUs(void) {
    return (uint32_t)COIN_PULSE_MIN_GAP_MS * 1000UL;
}

void test_clean_30ms_pulses_count_1_each(void) {
    PulseFilter filter(minGapUs());
    // One token per second, as the Hopper U-II delivers them.
    const uint32_t edges[] = { 0, 1000000UL, 2000000UL, 3000000UL };

    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(filter.accept(edges[i]),
            "A clean pulse a second after the last one is a token");
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, filter.accepted(), "Four coins, four tokens");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, filter.rejected(), "… and nothing thrown away");
}

void test_bounce_burst_within_5ms_counts_once(void) {
    PulseFilter filter(minGapUs());
    // The simulator's bounce mode: three edges 2 ms apart, then the real
    // 30 ms pulse 4 ms later.  One coin fell.
    const uint32_t edges[] = { 0, 2000, 4000, 8000 };

    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        filter.accept(edges[i]);
    }

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, filter.accepted(),
        "A bouncing sensor delivers one coin, not four");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, filter.rejected(),
        "… and the three edges it threw away are counted, not hidden");
}

void test_pulses_2ms_apart_are_rejected(void) {
    PulseFilter filter(minGapUs());

    TEST_ASSERT_TRUE_MESSAGE(filter.accept(0), "The first edge is always a token");
    TEST_ASSERT_FALSE_MESSAGE(filter.accept(2000),
        "2 ms after a token is EMI, not a second coin");
    TEST_ASSERT_FALSE_MESSAGE(filter.accept(4000),
        "… and the deadline is measured from the accepted edge, not the last one, "
        "so a burst cannot walk it forward");
    TEST_ASSERT_TRUE_MESSAGE(filter.accept(minGapUs()),
        "Exactly the minimum gap after the token is a token again");
}

void test_the_minimum_gap_fits_the_datasheet_pulse(void) {
    // The number is derived, not chosen: one coin is a single LOW phase of
    // 30-65 ms, so the spacing has to sit below the shortest legal pulse —
    // otherwise a real coin arriving early is filtered away as noise.
    TEST_ASSERT_TRUE_MESSAGE(COIN_PULSE_MIN_GAP_MS > 0,
        "A gap of zero is the unfiltered ISR this issue is about");
    TEST_ASSERT_TRUE_MESSAGE(COIN_PULSE_MIN_GAP_MS < PULSE_DURATION_MS,
        "The gap must stay under the datasheet's 30 ms pulse");
}

void test_filter_counts_across_the_micros_wraparound(void) {
    // micros() wraps every ~71 minutes.  A dispenser that treats the wrap as a
    // huge gap is harmless; one that treats it as a tiny gap drops a token.
    PulseFilter filter(minGapUs());
    const uint32_t before_wrap = 0xFFFFFF00UL;

    // The casts are the point: the sum is what micros() would report after the
    // wrap, and the filter has to read it as a small difference, not a huge one.
    TEST_ASSERT_TRUE(filter.accept(before_wrap));
    TEST_ASSERT_FALSE_MESSAGE(filter.accept((uint32_t)(before_wrap + 2000UL)),
        "2 ms later is still noise, even when the counter wrapped in between");
    TEST_ASSERT_TRUE_MESSAGE(filter.accept((uint32_t)(before_wrap + minGapUs())),
        "… and a real coin after the wrap is still a coin");
}

void test_reset_forgets_the_last_edge(void) {
    PulseFilter filter(minGapUs());
    filter.accept(0);
    filter.accept(1000);        // noise

    filter.reset();

    TEST_ASSERT_TRUE_MESSAGE(filter.accept(1000),
        "After a reset the next edge is the first one of a new transaction");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, filter.accepted(), "… and the counters start over");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, filter.rejected(), "… both of them");
}

// =============================================================================
// The settling window and the overrun (issue #5)
//
// The motor is cut by the ISR the moment the target count is reached, but a
// token already past the wheel still falls.  It used to be counted by the ISR
// and then thrown away: loop() copied the count once, at completion, and
// `dispensed` could never exceed `quantity`.  The tokens were in the tray and
// invisible to the terminal and to the metrics.
// =============================================================================

void test_target_count_stops_the_motor_but_not_the_transaction(void) {
    manager->begin();
    startAndRun("tx_settle", 2);

    hopper->simulatePulseISR();
    hopper->simulatePulseISR();
    manager->loop();

    TEST_ASSERT_FALSE_MESSAGE(hopper->isMotorRunning(),
        "The target count stops the motor immediately");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DISPENSING, manager->getActiveTransaction().state,
        "… but the transaction stays DISPENSING through the settling window: "
        "a token can still fall, and it must be counted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getSaveCalls(),
        "… and nothing is committed yet — the final count is not final yet");

    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DONE, manager->getTransaction("tx_settle").state,
        "Once the window is over the transaction is done");
    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(), "… and the device is idle again");
}

void test_coast_pulse_within_settling_window_is_counted_and_reported(void) {
    manager->begin();
    startAndRun("tx_coast", 2);

    hopper->simulatePulseISR();
    hopper->simulatePulseISR();
    manager->loop();                       // target reached, motor off, settling

    hopper->simulatePulseISR();            // the token that was already falling
    manager->loop();                       // still inside the window
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, counts->storedCount(),
        "The coast token reaches RTC memory like every other one, while it can "
        "still be recovered — the block is released only when the tx ends");

    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();

    Transaction tx = manager->getTransaction("tx_coast");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DONE, tx.state,
        "A coast token does not turn a good dispense into an error");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, tx.dispensed,
        "Three tokens left the hopper, so dispensed is 3 — greater than quantity "
        "is legal, and it is what the terminal bills");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, manager->getDispensedTokens(),
        "… and the token metric counts what came out, not what was asked for");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, manager->getOverrunTokens(),
        "… and the one token past the quantity is the overrun");
}

void test_overrun_increments_metric(void) {
    manager->begin();
    startAndRun("tx_over", 1);

    hopper->simulatePulseISR();
    manager->loop();
    hopper->simulatePulseISR();            // one token too many
    hopper->simulatePulseISR();            // and another
    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, manager->getOverrunTokens(),
        "Every token past the requested quantity is counted as an overrun");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, manager->getSuccessful(),
        "… and the transaction still counts as successful: the tokens came out");
}

void test_a_clean_dispense_has_no_overrun(void) {
    manager->begin();
    startAndRun("tx_clean", 3);

    hopper->setPulseCount(3);
    loopUntilDone();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, manager->getOverrunTokens(),
        "A dispense that delivered exactly what was asked for has no overrun");
}

void test_a_token_after_the_settling_window_is_not_billed_to_the_next_tx(void) {
    // The window ends the transaction.  Whatever the pulse counter does after
    // that belongs to nobody — and must not be carried into the next dispense,
    // which resets the counter before it starts the motor.
    manager->begin();
    startAndRun("tx_first", 1);
    hopper->simulatePulseISR();
    loopUntilDone();

    hopper->simulatePulseISR();            // far too late, after `done`

    startAndRun("tx_second", 1);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, manager->getActiveTransaction().dispensed,
        "A new transaction starts at zero, whatever the counter held");
}

void test_a_request_during_the_settling_window_is_busy(void) {
    manager->begin();
    startAndRun("tx_busy1", 1);

    hopper->simulatePulseISR();
    manager->loop();                       // settling: the motor is off, the tx is not done

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        DISPENSE_BUSY, manager->requestDispense("tx_busy2", 1),
        "Another transaction during the settling window is busy — tokens from the "
        "old one may still be falling, and they would be billed to the new one");
}

void test_settling_does_not_cost_a_third_commit(void) {
    manager->begin();
    storage->resetCallCounts();

    startAndRun("tx_cost2", 2);
    hopper->setPulseCount(2);
    manager->loop();                       // settling
    manager->loop();                       // still settling
    _mock_millis += DISPENSE_SETTLING_MS;
    manager->loop();                       // done

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        2, storage->getSaveCalls(),
        "The settling window is not a state transition: still two commits "
        "per transaction (owner decision, 2026-09-20)");
}

void test_a_jam_is_still_a_jam_while_below_the_target(void) {
    // The settling window must not swallow the jam watchdog: it only starts
    // once the target count has been reached.
    manager->begin();
    startAndRun("tx_jam2", 4);

    hopper->setPulseCount(1);
    hopper->setJamDetected(true);
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR, manager->getTransaction("tx_jam2").state,
        "A jam below the target still ends the transaction at once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0, manager->getOverrunTokens(), "… and a partial dispense is no overrun");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_crash_recovery_increments_crash_counter);
    RUN_TEST(test_no_crash_recovery_does_not_increment_counter);
    RUN_TEST(test_failures_calculation_includes_crashes);
    RUN_TEST(test_crash_recovery_preserves_token_counts);
    RUN_TEST(test_persisted_error_state_is_cleared_on_boot);

    RUN_TEST(test_requested_tokens_accumulates_across_transactions);
    RUN_TEST(test_dispensed_tokens_tracks_actual_dispensed);

    RUN_TEST(test_start_dispense_persists_and_starts_motor);
    RUN_TEST(test_busy_with_other_tx_is_rejected);
    RUN_TEST(test_jam_stops_motor_and_records_partial);
    RUN_TEST(test_completed_dispense_leaves_the_device_idle_and_faultless);
    RUN_TEST(test_unknown_tx_is_reported_as_empty);

    RUN_TEST(test_jam_sets_fault_and_blocks_new_dispense);
    RUN_TEST(test_idempotent_get_post_still_answer_while_faulted);
    RUN_TEST(test_crash_recovery_leaves_device_idle);
    RUN_TEST(test_hopper_error_during_dispense_aborts_before_jam_timeout);
    RUN_TEST(test_hopper_error_while_idle_faults_the_device);
    RUN_TEST(test_a_fault_in_the_settling_window_leaves_no_settling_behind);
    RUN_TEST(test_fault_is_not_persisted_and_boot_starts_idle);
    RUN_TEST(test_jam_after_reboot_sets_fault_again_with_zero_dispensed);
    RUN_TEST(test_device_state_matrix);
    RUN_TEST(test_error_kind_survives_a_reboot);

    RUN_TEST(test_accepted_request_touches_neither_flash_nor_motor);
    RUN_TEST(test_pending_request_is_started_by_loop_exactly_once);
    RUN_TEST(test_second_request_before_loop_is_busy);
    RUN_TEST(test_retry_before_loop_does_not_queue_a_second_start);

    RUN_TEST(test_body_in_one_chunk_is_complete);
    RUN_TEST(test_body_in_two_chunks_is_assembled_before_parsing);
    RUN_TEST(test_empty_body_is_complete_and_empty);
    RUN_TEST(test_body_over_the_cap_is_refused_not_truncated);
    RUN_TEST(test_chunk_with_a_gap_is_malformed);

    RUN_TEST(test_motor_stops_immediately_on_isr_pulse_without_loop);
    RUN_TEST(test_loop_completes_transaction_after_isr_stop);

    RUN_TEST(test_replay_of_finished_tx_returns_cached_result);
    RUN_TEST(test_retry_of_active_tx_returns_true_and_does_not_restart);
    RUN_TEST(test_idempotent_hit_does_not_touch_active_tx);
    RUN_TEST(test_idempotent_hit_while_idle_leaves_active_idle);
    RUN_TEST(test_same_tx_id_different_quantity_is_rejected);
    RUN_TEST(test_outcome_distinguishes_busy_from_a_reused_tx_id);

    RUN_TEST(test_reset_mid_dispense_recovers_rtc_count);
    RUN_TEST(test_power_loss_mid_dispense_reports_count_unreliable);
    RUN_TEST(test_live_count_goes_to_rtc_and_not_to_flash);
    RUN_TEST(test_done_tx_is_found_after_reboot);
    RUN_TEST(test_ring_survives_reboot_and_wraps_at_8);
    RUN_TEST(test_second_boot_after_crash_adds_no_empty_history_entry);
    RUN_TEST(test_successful_dispense_commits_twice);
    RUN_TEST(test_corrupt_record_is_ignored);
    RUN_TEST(test_old_layout_version_is_ignored);
    RUN_TEST(test_rtc_block_of_another_tx_is_not_this_count);
    RUN_TEST(test_sealed_record_validates_and_zeroed_one_does_not);
    RUN_TEST(test_sealed_rtc_block_validates_and_zeroed_one_does_not);

    RUN_TEST(test_clean_30ms_pulses_count_1_each);
    RUN_TEST(test_bounce_burst_within_5ms_counts_once);
    RUN_TEST(test_pulses_2ms_apart_are_rejected);
    RUN_TEST(test_the_minimum_gap_fits_the_datasheet_pulse);
    RUN_TEST(test_filter_counts_across_the_micros_wraparound);
    RUN_TEST(test_reset_forgets_the_last_edge);

    RUN_TEST(test_target_count_stops_the_motor_but_not_the_transaction);
    RUN_TEST(test_coast_pulse_within_settling_window_is_counted_and_reported);
    RUN_TEST(test_overrun_increments_metric);
    RUN_TEST(test_a_clean_dispense_has_no_overrun);
    RUN_TEST(test_a_token_after_the_settling_window_is_not_billed_to_the_next_tx);
    RUN_TEST(test_a_request_during_the_settling_window_is_busy);
    RUN_TEST(test_settling_does_not_cost_a_third_commit);
    RUN_TEST(test_a_jam_is_still_a_jam_while_below_the_target);

    RUN_TEST(test_a_connected_link_never_restarts);
    RUN_TEST(test_a_short_outage_is_ridden_out);
    RUN_TEST(test_a_minute_down_and_idle_restarts);
    RUN_TEST(test_a_failed_join_restarts_like_a_dropped_link);
    RUN_TEST(test_the_motor_outranks_the_supervisor);
    RUN_TEST(test_should_restart_is_the_whole_rule);
    RUN_TEST(test_a_reconnect_is_counted_and_starts_the_clock_over);
    RUN_TEST(test_the_outage_clock_survives_the_millis_wraparound);

    return UNITY_END();
}
