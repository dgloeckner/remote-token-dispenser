// firmware/dispenser/dispense_manager.cpp

#include "dispense_manager.h"
#include <string.h>

DispenseManager::DispenseManager(FlashStorage& storage, HopperControl& hopper)
  : flashStorage(storage), hopperControl(hopper) {
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;

  memset(tx_history, 0, sizeof(tx_history));
  memset(history_states, 0, sizeof(history_states));
  history_index = 0;

  total_dispenses = 0;
  successful_count = 0;
  jam_count = 0;
  partial_count = 0;
}

void DispenseManager::begin() {
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
      Serial.print("Recovered from crash during dispense. Partial count: ");
      Serial.println(active_tx.dispensed);
    } else if (active_tx.state == STATE_ERROR) {
      // Power cycled to clear jam - manual reset
      Serial.println("Clearing previous error state (manual reset via power cycle)");
      flashStorage.clear();
      memset(&active_tx, 0, sizeof(active_tx));
      active_tx.state = STATE_IDLE;
    }

    // Add to history
    addToHistory(active_tx.tx_id, active_tx.state);
  }
}

bool DispenseManager::startDispense(const char* tx_id, uint8_t quantity) {
  // Check if already in history (idempotency)
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    // Return cached result
    active_tx = cached_tx;
    return true;  // Not an error, just idempotent
  }

  // Check if busy
  if (active_tx.state == STATE_DISPENSING) {
    return false;  // 409 Conflict
  }

  // Start new transaction
  strncpy(active_tx.tx_id, tx_id, 16);
  active_tx.tx_id[16] = '\0';
  active_tx.quantity = quantity;
  active_tx.dispensed = 0;
  active_tx.state = STATE_DISPENSING;
  active_tx.started_ms = millis();

  // Persist to flash
  persistActiveTransaction();

  // Start motor
  hopperControl.resetPulseCount();
  hopperControl.startMotor();

  // Update metrics
  total_dispenses++;

  return true;
}

void DispenseManager::loop() {
  if (active_tx.state != STATE_DISPENSING) {
    return;  // Nothing to monitor
  }

  // Update dispensed count from pulse counter
  active_tx.dispensed = hopperControl.getPulseCount();

  // Check for completion
  if (active_tx.dispensed >= active_tx.quantity) {
    hopperControl.stopMotor();
    active_tx.state = STATE_DONE;
    persistActiveTransaction();
    addToHistory(active_tx.tx_id, STATE_DONE);
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
    addToHistory(active_tx.tx_id, STATE_ERROR);
    jam_count++;

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

// Private methods
bool DispenseManager::findInHistory(const char* tx_id, Transaction& out_tx) {
  for (int i = 0; i < RING_BUFFER_SIZE; i++) {
    if (strcmp(tx_history[i], tx_id) == 0) {
      // Found in history
      memset(&out_tx, 0, sizeof(out_tx));
      strncpy(out_tx.tx_id, tx_id, 16);
      out_tx.tx_id[16] = '\0';
      out_tx.state = history_states[i];
      // Note: quantity/dispensed not stored in history (would need expansion)
      return true;
    }
  }
  return false;
}

void DispenseManager::addToHistory(const char* tx_id, TransactionState state) {
  strncpy(tx_history[history_index], tx_id, 16);
  tx_history[history_index][16] = '\0';
  history_states[history_index] = state;
  history_index = (history_index + 1) % RING_BUFFER_SIZE;
}

void DispenseManager::persistActiveTransaction() {
  PersistedTransaction persisted;
  strncpy(persisted.tx_id, active_tx.tx_id, 16);
  persisted.tx_id[16] = '\0';
  persisted.quantity = active_tx.quantity;
  persisted.dispensed = active_tx.dispensed;
  persisted.state = active_tx.state;

  flashStorage.persist(persisted);
}
