// firmware/dispenser/dispense_manager.h

#ifndef DISPENSE_MANAGER_H
#define DISPENSE_MANAGER_H

#include <Arduino.h>
#include "flash_storage.h"
#include "hopper_control.h"

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
  DispenseManager(FlashStorage& storage, HopperControl& hopper);

  void begin();
  void loop();  // Called from main loop for watchdog

  // Transaction operations
  bool startDispense(const char* tx_id, uint8_t quantity);
  Transaction getTransaction(const char* tx_id);
  Transaction getActiveTransaction();
  bool isIdle();

  // Transaction-level metrics
  uint16_t getTotalDispenses();
  uint16_t getSuccessful();
  uint16_t getJams();
  uint16_t getPartial();
  uint16_t getCrashes();

  // Token-level metrics
  uint32_t getRequestedTokens();
  uint32_t getDispensedTokens();

private:
  FlashStorage& flashStorage;
  HopperControl& hopperControl;

  Transaction active_tx;

  // Ring buffer for idempotency (last 8 transactions with full data)
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
  uint16_t crash_count;

  // Token-level metrics
  uint32_t requested_tokens;
  uint32_t dispensed_tokens;

  bool findInHistory(const char* tx_id, Transaction& out_tx);
  void addToHistory(const char* tx_id, TransactionState state, uint8_t quantity, uint8_t dispensed);
  void persistActiveTransaction();
};

#endif
