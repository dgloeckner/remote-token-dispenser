// test_dispense_manager_impl.cpp
// This file includes the real DispenseManager implementation for testing

#include "mocks/Arduino.h"
#include "mocks/flash_storage_mock.h"
#include "mocks/hopper_control_mock.h"
#include <string.h>

#define RING_BUFFER_SIZE 8

struct Transaction {
  char tx_id[17];
  uint8_t quantity;
  uint8_t dispensed;
  TransactionState state;
  unsigned long started_ms;
};

class DispenseManager {
public:
  DispenseManager(FlashStorageMock& storage, HopperControlMock& hopper)
    : flashStorage(storage), hopperControl(hopper) {
    memset(&active_tx, 0, sizeof(active_tx));
    active_tx.state = STATE_IDLE;

    memset(history, 0, sizeof(history));
    history_index = 0;

    total_dispenses = 0;
    successful_count = 0;
    jam_count = 0;
    partial_count = 0;
    crash_count = 0;        // NEW
    requested_tokens = 0;   // NEW
    dispensed_tokens = 0;   // NEW
  }

  void begin() {
    // Load persisted transaction if exists
    if (flashStorage.hasPersistedTransaction()) {
      PersistedTransaction persisted = flashStorage.load();

      // Copy to active transaction
      strncpy(active_tx.tx_id, persisted.tx_id, 16);
      active_tx.tx_id[16] = '\0';
      active_tx.quantity = persisted.quantity;
      active_tx.dispensed = persisted.dispensed;
      active_tx.state = persisted.state;

      // Handle recovery scenarios
      if (active_tx.state == STATE_DISPENSING) {
        // Crashed during dispense - mark as error
        active_tx.state = STATE_ERROR;
        persistActiveTransaction();

        // NEW: Count the crashed transaction
        total_dispenses++;      // The transaction was started before crash
        crash_count++;          // Track crash specifically
        requested_tokens += active_tx.quantity;  // Add requested tokens
        dispensed_tokens += active_tx.dispensed; // Add partial dispense

        Serial.print("Recovered from crash during dispense. Partial count: ");
        Serial.println(active_tx.dispensed);
      } else if (active_tx.state == STATE_ERROR) {
        // Power cycled to clear jam - manual reset
        Serial.println("Clearing previous error state (manual reset via power cycle)");
        flashStorage.clear();
        memset(&active_tx, 0, sizeof(active_tx));
        active_tx.state = STATE_IDLE;
      }

      // Add to history with full transaction data
      addToHistory(active_tx.tx_id, active_tx.state, active_tx.quantity, active_tx.dispensed);
    }
  }

  bool startDispense(const char* tx_id, uint8_t quantity) {
    Serial.println("[DispenseManager] startDispense() called");

    // Check if already in history (idempotency)
    Transaction cached_tx;
    if (findInHistory(tx_id, cached_tx)) {
      Serial.println("  Transaction found in history (idempotent request)");
      active_tx = cached_tx;
      return true;
    }

    // Check if busy
    if (active_tx.state == STATE_DISPENSING) {
      Serial.println("  ERROR: Already dispensing, rejecting request");
      return false;
    }

    // Start new transaction
    Serial.println("  Starting new dispense transaction");
    strncpy(active_tx.tx_id, tx_id, 16);
    active_tx.tx_id[16] = '\0';
    active_tx.quantity = quantity;
    active_tx.dispensed = 0;
    active_tx.state = STATE_DISPENSING;
    active_tx.started_ms = millis();

    // Persist to flash
    persistActiveTransaction();

    // Arm ISR-level stop BEFORE starting motor
    hopperControl.resetPulseCount();
    hopperControl.setMotorStopAt(quantity);
    hopperControl.startMotor();

    // Update metrics
    total_dispenses++;
    requested_tokens += quantity;  // NEW: Track requested tokens

    return true;
  }

  void loop() {
    if (active_tx.state != STATE_DISPENSING) {
      return;
    }

    // Update dispensed count from pulse counter
    uint8_t previous_count = active_tx.dispensed;
    active_tx.dispensed = hopperControl.getPulseCount();

    // Check for completion
    if (active_tx.dispensed >= active_tx.quantity) {
      hopperControl.stopMotor();
      active_tx.state = STATE_DONE;

      hopperControl.errorHistory.clearActive();

      persistActiveTransaction();
      addToHistory(active_tx.tx_id, STATE_DONE, active_tx.quantity, active_tx.dispensed);

      // NEW: Track dispensed tokens
      dispensed_tokens += active_tx.dispensed;

      flashStorage.clear();
      memset(&active_tx, 0, sizeof(active_tx));
      active_tx.state = STATE_IDLE;
      successful_count++;
      return;
    }

    // Check for jam
    if (hopperControl.checkJam()) {
      hopperControl.stopMotor();
      active_tx.state = STATE_ERROR;
      persistActiveTransaction();
      addToHistory(active_tx.tx_id, STATE_ERROR, active_tx.quantity, active_tx.dispensed);
      jam_count++;

      // NEW: Track dispensed tokens even on jam (partial dispense)
      dispensed_tokens += active_tx.dispensed;

      if (active_tx.dispensed > 0) {
        partial_count++;
      }

      return;
    }
  }

  Transaction getTransaction(const char* tx_id) {
    if (strcmp(active_tx.tx_id, tx_id) == 0) {
      return active_tx;
    }

    Transaction cached_tx;
    if (findInHistory(tx_id, cached_tx)) {
      return cached_tx;
    }

    Transaction empty_tx;
    memset(&empty_tx, 0, sizeof(empty_tx));
    empty_tx.state = STATE_IDLE;
    return empty_tx;
  }

  Transaction getActiveTransaction() {
    return active_tx;
  }

  bool isIdle() {
    return active_tx.state != STATE_DISPENSING;
  }

  // Transaction-level metrics
  uint16_t getTotalDispenses() { return total_dispenses; }
  uint16_t getSuccessful() { return successful_count; }
  uint16_t getJams() { return jam_count; }
  uint16_t getPartial() { return partial_count; }
  uint16_t getCrashes() { return crash_count; }  // NEW

  // Token-level metrics (NEW)
  uint32_t getRequestedTokens() { return requested_tokens; }
  uint32_t getDispensedTokens() { return dispensed_tokens; }

private:
  FlashStorageMock& flashStorage;
  HopperControlMock& hopperControl;

  Transaction active_tx;

  struct HistoryEntry {
    char tx_id[17];
    TransactionState state;
    uint8_t quantity;
    uint8_t dispensed;
  };
  HistoryEntry history[RING_BUFFER_SIZE];
  uint8_t history_index;

  // Transaction-level metrics
  uint16_t total_dispenses;
  uint16_t successful_count;
  uint16_t jam_count;
  uint16_t partial_count;
  uint16_t crash_count;  // NEW

  // Token-level metrics (NEW)
  uint32_t requested_tokens;
  uint32_t dispensed_tokens;

  bool findInHistory(const char* tx_id, Transaction& out_tx) {
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
      if (strcmp(history[i].tx_id, tx_id) == 0) {
        memset(&out_tx, 0, sizeof(out_tx));
        strncpy(out_tx.tx_id, tx_id, 16);
        out_tx.tx_id[16] = '\0';
        out_tx.state = history[i].state;
        out_tx.quantity = history[i].quantity;
        out_tx.dispensed = history[i].dispensed;
        return true;
      }
    }
    return false;
  }

  void addToHistory(const char* tx_id, TransactionState state, uint8_t quantity, uint8_t dispensed) {
    strncpy(history[history_index].tx_id, tx_id, 16);
    history[history_index].tx_id[16] = '\0';
    history[history_index].state = state;
    history[history_index].quantity = quantity;
    history[history_index].dispensed = dispensed;
    history_index = (history_index + 1) % RING_BUFFER_SIZE;
  }

  void persistActiveTransaction() {
    PersistedTransaction persisted;
    strncpy(persisted.tx_id, active_tx.tx_id, 16);
    persisted.tx_id[16] = '\0';
    persisted.quantity = active_tx.quantity;
    persisted.dispensed = active_tx.dispensed;
    persisted.state = active_tx.state;

    flashStorage.persist(persisted);
  }
};
