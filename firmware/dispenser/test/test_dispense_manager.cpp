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

#include "crash_state.h"
#include "dispense_manager.h"
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

// tx_id for the ring tests: "ring1" … "ring9", without pulling in <stdio.h>.
static void snprintfTxId(char* out, int n) {
    out[0] = 'r'; out[1] = 'i'; out[2] = 'n'; out[3] = 'g';
    out[4] = (char)('0' + n);
    out[5] = '\0';
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
    manager->loop();                     // completes tx001

    startAndRun("tx002", 3);  // now accepted

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        8, manager->getRequestedTokens(),
        "Requested tokens should accumulate across all transactions");
}

void test_dispensed_tokens_tracks_actual_dispensed(void) {
    manager->begin();

    startAndRun("tx001", 5);
    hopper->setPulseCount(5);
    manager->loop();                     // full success

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

void test_completed_dispense_clears_active_hopper_error(void) {
    // Self-healing per dispenser-protocol.md § Design Principles 5.
    manager->begin();
    startAndRun("tx_heal", 1);

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
    startAndRun("tx_done", 2);
    hopper->setPulseCount(2);
    manager->loop();                     // tx_done is DONE and in the ring

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
    manager->loop();                      // tx_old DONE, lands in the ring

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
    manager->loop();                      // idle again, tx_done in the ring

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
    manager->loop();                      // tx_qty DONE with quantity 2

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
    manager->loop();

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
    manager->loop();                     // DONE

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
        manager->loop();
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
    manager->loop();

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
    manager->loop();

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
    manager->loop();

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

    RUN_TEST(test_accepted_request_touches_neither_flash_nor_motor);
    RUN_TEST(test_pending_request_is_started_by_loop_exactly_once);
    RUN_TEST(test_second_request_before_loop_is_busy);
    RUN_TEST(test_retry_before_loop_does_not_queue_a_second_start);

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

    return UNITY_END();
}
