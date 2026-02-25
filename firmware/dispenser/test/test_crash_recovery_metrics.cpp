// test_crash_recovery_metrics.cpp
// Tests for crash recovery metric tracking bug fix

#include <unity.h>
#include <string.h>

// Define UNIT_TEST before including production code
#define UNIT_TEST

// Include the test implementation (includes mocks + DispenseManager)
#include "test_dispense_manager_impl.cpp"

// Test setup
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

// RED: Test that crash recovery increments crash counter
void test_crash_recovery_increments_crash_counter(void) {
    // Arrange: Simulate a crash during dispense by persisting a transaction
    // in STATE_DISPENSING state
    storage->setPersistedTransaction(
        "tx_crash_001",  // tx_id
        5,               // quantity (wanted 5 tokens)
        2,               // dispensed (only got 2 before crash)
        STATE_DISPENSING // state (was dispensing when crashed)
    );

    // Act: Call begin() which should detect and recover from the crash
    manager->begin();

    // Assert: Crash counter should be incremented
    uint16_t crashes = manager->getCrashes();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1,
        crashes,
        "Crash recovery should increment crash counter"
    );

    // Also verify the transaction was marked as ERROR
    Transaction recovered = manager->getTransaction("tx_crash_001");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_ERROR,
        recovered.state,
        "Recovered transaction should be in ERROR state"
    );

    // Verify partial dispense was recorded
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2,
        recovered.dispensed,
        "Recovered transaction should preserve dispensed count"
    );
}

// Additional test: No crash counter increment if no crash
void test_no_crash_recovery_does_not_increment_counter(void) {
    // Arrange: No persisted transaction (clean start)
    // storage is empty by default

    // Act: Call begin()
    manager->begin();

    // Assert: Crash counter should remain 0
    uint16_t crashes = manager->getCrashes();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0,
        crashes,
        "No crash recovery means crash counter stays zero"
    );
}

// Additional test: Failures calculation should include crashes
void test_failures_calculation_includes_crashes(void) {
    // Arrange: Simulate a crash
    storage->setPersistedTransaction(
        "tx_crash_002",
        3,
        1,
        STATE_DISPENSING
    );

    // Act
    manager->begin();

    // Assert: total_dispenses calculation
    // When the transaction originally started, total_dispenses was incremented
    // So after recovery: total = 1 (the original start)
    // crashes = 1, successful = 0, jams = 0
    // Therefore: failures = total - successful - jams = 1 - 0 - 0 = 1
    // And this failure is a crash

    uint16_t total = manager->getTotalDispenses();
    uint16_t successful = manager->getSuccessful();
    uint16_t jams = manager->getJams();
    uint16_t crashes = manager->getCrashes();

    uint16_t failures = total - successful - jams;

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        crashes,
        failures,
        "Failures should equal crashes when no other failure types"
    );
}

// Token counter tests
void test_requested_tokens_accumulates_across_transactions(void) {
    // Arrange & Act: Start multiple dispense transactions
    manager->begin();
    manager->startDispense("tx001", 5);  // Request 5 tokens
    manager->startDispense("tx002", 3);  // Request 3 more (should fail - busy)

    // Complete first transaction
    hopper->setPulseCount(5);
    manager->loop(); // Should complete successfully

    manager->startDispense("tx002", 3);  // Now should work

    // Assert: Total requested should be 5 + 3 = 8
    uint32_t requested = manager->getRequestedTokens();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        8,
        requested,
        "Requested tokens should accumulate across all transactions"
    );
}

void test_dispensed_tokens_tracks_actual_dispensed(void) {
    // Arrange
    manager->begin();

    // Act: Transaction 1 - full success (5/5 tokens)
    manager->startDispense("tx001", 5);
    hopper->setPulseCount(5);
    manager->loop(); // Complete

    // Transaction 2 - partial (2/3 tokens then jam)
    manager->startDispense("tx002", 3);
    hopper->setPulseCount(2);
    hopper->setJamDetected(true);
    manager->loop(); // Jam with partial dispense

    // Assert: Dispensed should be 5 + 2 = 7 (even though requested 8)
    uint32_t dispensed = manager->getDispensedTokens();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        7,
        dispensed,
        "Dispensed tokens should count actual tokens dispensed, including partials"
    );
}

void test_crash_recovery_preserves_token_counts(void) {
    // Arrange: Simulate a crash after 2 tokens of a 5-token request
    storage->setPersistedTransaction("tx_crash", 5, 2, STATE_DISPENSING);

    // Assume the transaction had already incremented requested_tokens before crash
    // In real scenario, requested_tokens would have been incremented when
    // startDispense() was originally called (before the crash)

    // Act: Recover from crash
    manager->begin();

    // Assert: The 2 dispensed tokens should be preserved
    // Note: This test verifies crash recovery doesn't corrupt the counts
    // The actual requested_tokens increment happens in startDispense(),
    // not during recovery
    Transaction recovered = manager->getTransaction("tx_crash");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        2,
        recovered.dispensed,
        "Crash recovery should preserve dispensed count"
    );
}

// =============================================================================
// Immediate motor stop (double-dispense regression) tests
// =============================================================================
//
// Root cause: motor was stopped only on the next loop() iteration (up to 10ms
// later). During that window the disc kept spinning and could push a 2nd coin
// through the exit chute.
//
// Fix: arm the ISR with the target count so it writes MOTOR_PIN LOW the
// instant pulse_count reaches the target, before the main loop even runs.

// RED: startDispense must arm the ISR with the target count
void test_startDispense_arms_isr_stop_target(void) {
    manager->begin();
    manager->startDispense("tx_isr001", 1);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1,
        hopper->getMotorStopAt(),
        "startDispense must set ISR stop target to the requested quantity"
    );
}

// RED: Motor must stop at ISR level when pulse count hits target,
// BEFORE loop() is called — this is the core fix for the double-dispense bug.
void test_motor_stops_immediately_on_isr_pulse_without_loop(void) {
    manager->begin();
    manager->startDispense("tx_isr002", 1);

    TEST_ASSERT_TRUE_MESSAGE(
        hopper->isMotorRunning(),
        "Motor should be running after startDispense"
    );

    // Simulate the coin-pulse ISR firing — motor must stop NOW,
    // without needing a loop() call
    hopper->simulatePulseISR();

    TEST_ASSERT_FALSE_MESSAGE(
        hopper->isMotorRunning(),
        "Motor must stop at ISR level, not wait for the next loop() iteration"
    );
}

// ISR stop target is cleared when the ISR stops the motor,
// preventing redundant stops on subsequent pulses from motor coasting
void test_isr_stop_target_cleared_after_isr_stop(void) {
    manager->begin();
    manager->startDispense("tx_isr003", 1);
    // Explicitly arm so the test is non-trivial even before startDispense()
    // is wired up to call setMotorStopAt()
    hopper->setMotorStopAt(1);

    hopper->simulatePulseISR();

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0,
        hopper->getMotorStopAt(),
        "ISR stop target must be cleared once the motor is stopped"
    );
}

// Sanity: loop() still transitions state to DONE after ISR stops motor
void test_loop_completes_transaction_after_isr_stop(void) {
    manager->begin();
    manager->startDispense("tx_isr004", 1);

    // ISR fires: motor stops immediately
    hopper->simulatePulseISR();

    // Main loop runs: should see count >= quantity and finish the transaction
    manager->loop();

    Transaction tx = manager->getTransaction("tx_isr004");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STATE_DONE,
        tx.state,
        "Transaction should be STATE_DONE after ISR stop + loop()"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        1,
        tx.dispensed,
        "Dispensed count should be 1"
    );
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Crash recovery tests
    RUN_TEST(test_crash_recovery_increments_crash_counter);
    RUN_TEST(test_no_crash_recovery_does_not_increment_counter);
    RUN_TEST(test_failures_calculation_includes_crashes);

    // Token counter tests
    RUN_TEST(test_requested_tokens_accumulates_across_transactions);
    RUN_TEST(test_dispensed_tokens_tracks_actual_dispensed);
    RUN_TEST(test_crash_recovery_preserves_token_counts);

    // Immediate motor stop (double-dispense regression) tests
    RUN_TEST(test_startDispense_arms_isr_stop_target);
    RUN_TEST(test_motor_stops_immediately_on_isr_pulse_without_loop);
    RUN_TEST(test_isr_stop_target_cleared_after_isr_stop);
    RUN_TEST(test_loop_completes_transaction_after_isr_stop);

    return UNITY_END();
}
