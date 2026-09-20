// firmware/dispenser/dispense_manager.cpp

#include "dispense_manager.h"
#include <string.h>

DispenseManager::DispenseManager(IStorage& storage, IHopper& hopper, ICountMemory& counts)
  : flashStorage(storage), hopperControl(hopper), countMemory(counts) {
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;
  active_tx.count_reliable = true;

  memset(history, 0, sizeof(history));
  history_index = 0;

  total_dispenses = 0;
  successful_count = 0;
  jam_count = 0;
  partial_count = 0;
  crash_count = 0;
  requested_tokens = 0;
  dispensed_tokens = 0;
}

void DispenseManager::begin() {
  // Load persisted record if exists
  PersistedRecord record;
  if (flashStorage.load(record)) {
    const PersistedTransaction& persisted = record.active;

    // Copy to active transaction
    strncpy(active_tx.tx_id, persisted.tx_id, 16);
    active_tx.tx_id[16] = '\0';
    active_tx.quantity = persisted.quantity;
    active_tx.dispensed = persisted.dispensed;
    active_tx.state = (TransactionState)persisted.state;
    active_tx.count_reliable = persisted.count_reliable != 0;

    // Handle recovery scenarios
    if (active_tx.state == STATE_DISPENSING) {
      // Crashed during dispense - mark as error
      active_tx.state = STATE_ERROR;
      persistState();

      // Count the crashed transaction
      total_dispenses++;                   // Transaction was started before crash
      crash_count++;                       // Track crash specifically
      requested_tokens += active_tx.quantity;   // Add requested tokens
      dispensed_tokens += active_tx.dispensed;  // Add partial dispense

      Serial.print("Recovered from crash during dispense. Partial count: ");
      Serial.println(active_tx.dispensed);
    } else if (active_tx.state == STATE_ERROR) {
      // Power cycled to clear jam - manual reset
      Serial.println("Clearing previous error state (manual reset via power cycle)");
      flashStorage.clear();
      memset(&active_tx, 0, sizeof(active_tx));
      active_tx.state = STATE_IDLE;
      active_tx.count_reliable = true;
    }

    // Add to history with full transaction data
    addToHistory(active_tx);
  }
}

DispenseOutcome DispenseManager::requestDispense(const char* tx_id, uint8_t quantity) {
  Serial.println("[DispenseManager] requestDispense() called");
  Serial.print("  tx_id: ");
  Serial.println(tx_id);
  Serial.print("  quantity: ");
  Serial.println(quantity);

  // Idempotency, first half: the transaction that is running right now.  It is
  // not in the history ring yet — entries land there when a transaction
  // finishes — so it has to be recognised here, or the busy check below would
  // answer 409 to the caller's own retry while its tokens are falling.
  if (active_tx.state != STATE_IDLE && strcmp(active_tx.tx_id, tx_id) == 0) {
    if (active_tx.quantity != quantity) {
      Serial.println("  ERROR: tx_id of the active transaction reused with another quantity");
      return DISPENSE_TX_ID_REUSED;
    }
    Serial.println("  Retry of the active transaction (idempotent request)");
    return DISPENSE_IDEMPOTENT;
  }

  // Idempotency, second half: a finished transaction in the ring.  active_tx
  // is deliberately NOT touched here.  Overwriting it used to switch loop()
  // off while the motor was running (no jam watchdog), and to make an idle
  // device report the state of some old transaction in /health.
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    if (cached_tx.quantity != quantity) {
      Serial.println("  ERROR: known tx_id reused with another quantity");
      return DISPENSE_TX_ID_REUSED;
    }
    Serial.println("  Transaction found in history (idempotent request)");
    return DISPENSE_IDEMPOTENT;
  }

  // Check if busy.  From here on a 409 always means ANOTHER transaction.
  if (active_tx.state == STATE_DISPENSING) {
    Serial.println("  ERROR: another transaction is dispensing, rejecting request");
    return DISPENSE_BUSY;
  }

  // Start new transaction
  Serial.println("  Starting new dispense transaction");
  strncpy(active_tx.tx_id, tx_id, 16);
  active_tx.tx_id[16] = '\0';
  active_tx.quantity = quantity;
  active_tx.dispensed = 0;
  active_tx.state = STATE_DISPENSING;
  active_tx.count_reliable = true;
  active_tx.started_ms = millis();

  // Persist to flash
  Serial.println("  Persisting transaction to flash...");
  persistState();

  // Arm ISR-level stop BEFORE starting motor so the very first pulse that
  // reaches the target immediately cuts motor power, eliminating the ~10ms
  // main-loop latency that was causing occasional double-dispenses.
  Serial.println("  Resetting pulse count and starting motor...");
  hopperControl.resetPulseCount();
  hopperControl.setMotorStopAt(quantity);
  hopperControl.startMotor();

  // Update metrics
  total_dispenses++;
  requested_tokens += quantity;

  Serial.println("[DispenseManager] Dispense started successfully");
  return DISPENSE_STARTED;
}

bool DispenseManager::startDispense(const char* tx_id, uint8_t quantity) {
  DispenseOutcome outcome = requestDispense(tx_id, quantity);
  return outcome == DISPENSE_STARTED || outcome == DISPENSE_IDEMPOTENT;
}

void DispenseManager::loop() {
  if (active_tx.state != STATE_DISPENSING) {
    return;  // Nothing to monitor
  }

  // Update dispensed count from pulse counter
  uint8_t previous_count = active_tx.dispensed;
  active_tx.dispensed = hopperControl.getPulseCount();

  // Log pulse count changes
  if (active_tx.dispensed != previous_count) {
    Serial.print("[DispenseManager] Pulse count: ");
    Serial.print(active_tx.dispensed);
    Serial.print(" / ");
    Serial.println(active_tx.quantity);
  }

  // Check for completion
  if (active_tx.dispensed >= active_tx.quantity) {
    Serial.println("[DispenseManager] Dispense COMPLETE!");
    hopperControl.stopMotor();
    active_tx.state = STATE_DONE;

    // Clear active error on successful completion (self-healing)
    hopperControl.clearActiveError();

    persistState();
    addToHistory(active_tx);

    // Track dispensed tokens
    dispensed_tokens += active_tx.dispensed;

    flashStorage.clear();
    memset(&active_tx, 0, sizeof(active_tx));
    active_tx.state = STATE_IDLE;
    active_tx.count_reliable = true;
    successful_count++;
    Serial.println("[DispenseManager] Dispense complete - active error cleared");
    return;
  }

  // Check for jam
  if (hopperControl.checkJam()) {
    Serial.println("[DispenseManager] JAM DETECTED!");
    Serial.print("  Dispensed: ");
    Serial.print(active_tx.dispensed);
    Serial.print(" / ");
    Serial.println(active_tx.quantity);
    hopperControl.stopMotor();
    active_tx.state = STATE_ERROR;
    persistState();
    addToHistory(active_tx);
    jam_count++;

    // Track dispensed tokens even on jam (partial dispense)
    dispensed_tokens += active_tx.dispensed;

    if (active_tx.dispensed > 0) {
      partial_count++;
    }

    // Stay in ERROR state - requires power cycle to clear
    return;
  }
}

Transaction DispenseManager::getTransaction(const char* tx_id) {
  // Check active transaction
  if (strcmp(active_tx.tx_id, tx_id) == 0) {
    return active_tx;
  }

  // Check history
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    return cached_tx;
  }

  // Not found - return empty with IDLE state
  Transaction empty_tx;
  memset(&empty_tx, 0, sizeof(empty_tx));
  empty_tx.state = STATE_IDLE;
  empty_tx.count_reliable = true;
  return empty_tx;
}

Transaction DispenseManager::getActiveTransaction() {
  return active_tx;
}

bool DispenseManager::isIdle() {
  return active_tx.state != STATE_DISPENSING;
}

uint16_t DispenseManager::getTotalDispenses() { return total_dispenses; }
uint16_t DispenseManager::getSuccessful() { return successful_count; }
uint16_t DispenseManager::getJams() { return jam_count; }
uint16_t DispenseManager::getPartial() { return partial_count; }
uint16_t DispenseManager::getCrashes() { return crash_count; }
uint32_t DispenseManager::getRequestedTokens() { return requested_tokens; }
uint32_t DispenseManager::getDispensedTokens() { return dispensed_tokens; }

// Private methods
bool DispenseManager::findInHistory(const char* tx_id, Transaction& out_tx) {
  if (tx_id == NULL || tx_id[0] == '\0') {
    return false;
  }
  for (int i = 0; i < RING_BUFFER_SIZE; i++) {
    if (strcmp(history[i].tx_id, tx_id) == 0) {
      // Found in history - return complete transaction data
      memset(&out_tx, 0, sizeof(out_tx));
      strncpy(out_tx.tx_id, tx_id, 16);
      out_tx.tx_id[16] = '\0';
      out_tx.state = (TransactionState)history[i].state;
      out_tx.quantity = history[i].quantity;
      out_tx.dispensed = history[i].dispensed;
      out_tx.count_reliable = history[i].count_reliable != 0;
      return true;
    }
  }
  return false;
}

void DispenseManager::addToHistory(const Transaction& tx) {
  strncpy(history[history_index].tx_id, tx.tx_id, 16);
  history[history_index].tx_id[16] = '\0';
  history[history_index].state = (uint8_t)tx.state;
  history[history_index].quantity = tx.quantity;
  history[history_index].dispensed = tx.dispensed;
  history[history_index].count_reliable = tx.count_reliable ? 1 : 0;
  history_index = (history_index + 1) % RING_BUFFER_SIZE;
}

void DispenseManager::persistState() {
  PersistedRecord record;
  memset(&record, 0, sizeof(record));

  strncpy(record.active.tx_id, active_tx.tx_id, 16);
  record.active.tx_id[16] = '\0';
  record.active.quantity = active_tx.quantity;
  record.active.dispensed = active_tx.dispensed;
  record.active.state = (uint8_t)active_tx.state;
  record.active.count_reliable = active_tx.count_reliable ? 1 : 0;

  memcpy(record.ring, history, sizeof(record.ring));
  record.ring_index = history_index;

  flashStorage.save(record);
}
