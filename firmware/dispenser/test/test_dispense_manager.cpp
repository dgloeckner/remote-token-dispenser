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

#include "dispense_manager.h"
#include "mocks/flash_storage_mock.h"
#include "mocks/hopper_control_mock.h"

FlashStorageMock* storage;
HopperControlMock* hopper;
DispenseManager* manager;

void setUp(void) {
    storage = new FlashStorageMock();
    hopper = new HopperControlMock();
    manager = new DispenseManager(*storage, *hopper);
    _mock_millis = 0;
}

void tearDown(void) {
    delete manager;
    delete hopper;
    delete storage;
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
    manager->startDispense("tx001", 5);
    manager->startDispense("tx002", 3);  // busy → rejected

    hopper->setPulseCount(5);
    manager->loop();                     // completes tx001

    manager->startDispense("tx002", 3);  // now accepted

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        8, manager->getRequestedTokens(),
        "Requested tokens should accumulate across all transactions");
}

void test_dispensed_tokens_tracks_actual_dispensed(void) {
    manager->begin();

    manager->startDispense("tx001", 5);
    hopper->setPulseCount(5);
    manager->loop();                     // full success

    manager->startDispense("tx002", 3);
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

    TEST_ASSERT_TRUE(manager->startDispense("tx_start", 3));

    TEST_ASSERT_TRUE_MESSAGE(hopper->isMotorRunning(), "Motor must be running");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        3, hopper->getMotorStopAt(),
        "startDispense must arm the ISR stop with the requested quantity");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, storage->getPersistCalls(),
        "The started transaction must be persisted exactly once");
    TEST_ASSERT_FALSE_MESSAGE(manager->isIdle(), "Manager is busy while dispensing");
}

void test_busy_with_other_tx_is_rejected(void) {
    manager->begin();
    manager->startDispense("tx_a", 3);

    TEST_ASSERT_FALSE_MESSAGE(
        manager->startDispense("tx_b", 2),
        "A different tx_id while dispensing must be rejected (409 busy)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "A rejected request must not start the motor a second time");
}

void test_jam_stops_motor_and_records_partial(void) {
    manager->begin();
    manager->startDispense("tx_jam", 4);

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

void test_completed_dispense_clears_active_hopper_error(void) {
    // Self-healing per dispenser-protocol.md § Design Principles 5.
    manager->begin();
    manager->startDispense("tx_heal", 1);

    hopper->simulatePulseISR();
    manager->loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getClearErrorCalls(),
        "A completed dispense must clear the active hopper error");
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
    manager->startDispense("tx_done", 2);
    hopper->setPulseCount(2);
    manager->loop();                     // tx_done is DONE and in the ring

    TEST_ASSERT_TRUE_MESSAGE(
        manager->startDispense("tx_done", 2),
        "Replaying a finished tx_id is not an error");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "A replay must not dispense again");

    Transaction tx = manager->getTransaction("tx_done");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DONE, tx.state, "Cached state is returned");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, tx.dispensed, "Cached count is returned");
}

// ---------------------------------------------------------------------------
// KNOWN DEVIATION from dispenser-protocol.md, tracked in issue #2.
//
// The protocol says a retry of the *active* transaction returns its current
// state (200).  The production code looks the tx_id up in the history ring
// only, and a running transaction is not in the ring yet — so the busy check
// answers 409 for the caller's own transaction.
//
// This test pins TODAY's production behaviour on purpose, so that CI is green
// on a foundation PR that changes no behaviour.  #2 inverts it into
// `retry_of_active_tx_returns_true_and_does_not_restart`: assert `true`, one
// startMotor call, one resetPulseCount call, one persist call.
//
// That this test can distinguish the two at all is the point of #1: the
// assertions below run against dispense_manager.cpp, not against a copy.
// ---------------------------------------------------------------------------
void test_retry_of_active_tx_is_rejected_today_KNOWN_DEVIATION_issue_2(void) {
    manager->begin();
    manager->startDispense("tx_active", 5);

    bool retry = manager->startDispense("tx_active", 5);

    TEST_ASSERT_FALSE_MESSAGE(
        retry,
        "TODAY the retry of the active tx is rejected (409). #2 makes this true.");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "Whatever the verdict, the retry must never start the motor twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getResetPulseCalls(),
        "A retry must never reset the pulse counter of a running dispense");
}

// Second half of #2: an idempotent hit must not overwrite the active
// transaction.  Also a known deviation today — pinned, not asserted as correct.
void test_replay_while_other_tx_dispensing_overwrites_active_KNOWN_DEVIATION_issue_2(void) {
    manager->begin();

    manager->startDispense("tx_old", 1);
    hopper->simulatePulseISR();
    manager->loop();                      // tx_old DONE, lands in the ring

    manager->startDispense("tx_new", 5);  // now dispensing
    manager->startDispense("tx_old", 1);  // idempotent hit for the finished one

    Transaction active = manager->getActiveTransaction();
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "tx_old", active.tx_id,
        "TODAY the idempotent hit replaces active_tx — the jam watchdog is off "
        "while the motor runs. #2 makes this \"tx_new\".");
}

// =============================================================================
// Immediate motor stop (double-dispense regression, commit a9f15af)
// =============================================================================

void test_motor_stops_immediately_on_isr_pulse_without_loop(void) {
    manager->begin();
    manager->startDispense("tx_isr002", 1);
    TEST_ASSERT_TRUE_MESSAGE(hopper->isMotorRunning(), "Motor runs after startDispense");

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
    manager->startDispense("tx_isr004", 1);

    hopper->simulatePulseISR();
    manager->loop();

    Transaction tx = manager->getTransaction("tx_isr004");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DONE, tx.state,
        "Transaction should be STATE_DONE after ISR stop + loop()");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx.dispensed, "Dispensed count should be 1");
    TEST_ASSERT_TRUE_MESSAGE(manager->isIdle(), "Manager is idle again");
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
    RUN_TEST(test_completed_dispense_clears_active_hopper_error);
    RUN_TEST(test_unknown_tx_is_reported_as_empty);

    RUN_TEST(test_motor_stops_immediately_on_isr_pulse_without_loop);
    RUN_TEST(test_loop_completes_transaction_after_isr_stop);

    RUN_TEST(test_replay_of_finished_tx_returns_cached_result);
    RUN_TEST(test_retry_of_active_tx_is_rejected_today_KNOWN_DEVIATION_issue_2);
    RUN_TEST(test_replay_while_other_tx_dispensing_overwrites_active_KNOWN_DEVIATION_issue_2);

    return UNITY_END();
}
