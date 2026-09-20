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
    manager->startDispense("tx_active", 5);

    bool retry = manager->startDispense("tx_active", 5);

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
        1, storage->getPersistCalls(),
        "A retry must not write the transaction to flash a second time");

    Transaction active = manager->getActiveTransaction();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("tx_active", active.tx_id,
        "The running transaction stays the active one");
    TEST_ASSERT_EQUAL_INT_MESSAGE(STATE_DISPENSING, active.state,
        "… and stays in DISPENSING, so loop() keeps watching the motor");
}

void test_idempotent_hit_does_not_touch_active_tx(void) {
    manager->begin();

    manager->startDispense("tx_old", 1);
    hopper->simulatePulseISR();
    manager->loop();                      // tx_old DONE, lands in the ring

    manager->startDispense("tx_new", 5);  // now dispensing
    manager->startDispense("tx_old", 1);  // idempotent hit for the finished one

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
    manager->startDispense("tx_done", 1);
    hopper->simulatePulseISR();
    manager->loop();                      // idle again, tx_done in the ring

    manager->startDispense("tx_done", 1);

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
    manager->startDispense("tx_run", 3);

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
    manager->startDispense("tx_qty", 2);
    hopper->simulatePulseISR();
    hopper->simulatePulseISR();
    manager->loop();                      // tx_qty DONE with quantity 2

    TEST_ASSERT_FALSE_MESSAGE(
        manager->startDispense("tx_qty", 5),
        "The same tx_id with another quantity is a client bug, not a retry");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, hopper->getStartMotorCalls(),
        "… and must not dispense anything");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2, manager->getTransaction("tx_qty").quantity,
        "… and must not silently answer with the old quantity as if it matched");
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
    RUN_TEST(test_retry_of_active_tx_returns_true_and_does_not_restart);
    RUN_TEST(test_idempotent_hit_does_not_touch_active_tx);
    RUN_TEST(test_idempotent_hit_while_idle_leaves_active_idle);
    RUN_TEST(test_same_tx_id_different_quantity_is_rejected);
    RUN_TEST(test_outcome_distinguishes_busy_from_a_reused_tx_id);

    return UNITY_END();
}
