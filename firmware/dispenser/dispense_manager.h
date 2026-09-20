// firmware/dispenser/dispense_manager.h

#ifndef DISPENSE_MANAGER_H
#define DISPENSE_MANAGER_H

#include <Arduino.h>
#include "dispenser_types.h"
#include "interfaces.h"

#define RING_BUFFER_SIZE PERSIST_RING_SIZE

struct Transaction {
  char tx_id[17];
  uint8_t quantity;
  uint8_t dispensed;
  TransactionState state;
  // false means `dispensed` is a lower bound, not a fact: the device lost
  // power mid-dispense and the live count went with it.  Reported on every
  // transaction response (dispenser-protocol.md).
  bool count_reliable;
  unsigned long started_ms;
};

class DispenseManager {
public:
  DispenseManager(IStorage& storage, IHopper& hopper, ICountMemory& countMemory);

  void begin();
  void loop();  // Called from main loop for watchdog

  // Transaction operations.
  // requestDispense() is the full answer (see DispenseOutcome);
  // startDispense() is the same call reduced to "accepted or not".
  DispenseOutcome requestDispense(const char* tx_id, uint8_t quantity);
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
  IStorage& flashStorage;
  IHopper& hopperControl;
  ICountMemory& countMemory;

  Transaction active_tx;

  // Ring buffer for idempotency (last 8 transactions with full data).  It is
  // the same struct that goes to flash, so the ring is saved with the active
  // transaction in one commit and comes back on the next boot.
  PersistedTransaction history[RING_BUFFER_SIZE];
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
  void addToHistory(const Transaction& tx);
  // Writes the active transaction AND the ring in one commit.
  void persistState();
};

#endif
