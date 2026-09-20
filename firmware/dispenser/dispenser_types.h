// firmware/dispenser/dispenser_types.h
//
// Plain data shared by the production classes and by the native unit tests.
// Deliberately free of ESP SDK includes so that the native test build can
// compile the real production code (see firmware/dispenser/test/README.md).

#ifndef DISPENSER_TYPES_H
#define DISPENSER_TYPES_H

#include <stdint.h>

// Transaction state enum
enum TransactionState {
  STATE_IDLE = 0,
  STATE_DISPENSING = 1,
  STATE_DONE = 2,
  STATE_ERROR = 3
};

// Why a POST /dispense was answered the way it was.  The HTTP layer maps it
// to a status code; startDispense() collapses it back to a bool for the
// callers that only need "did this go wrong".
enum DispenseOutcome {
  DISPENSE_STARTED = 0,      // new transaction, motor running        → 200
  DISPENSE_IDEMPOTENT = 1,   // same tx_id and quantity as before     → 200
  DISPENSE_BUSY = 2,         // ANOTHER transaction is dispensing     → 409 busy
  DISPENSE_TX_ID_REUSED = 3  // known tx_id, different quantity       → 409 tx_id reused
};

// Persisted transaction structure
struct PersistedTransaction {
  char tx_id[17];           // "a3f8c012" + null terminator
  uint8_t quantity;         // 1-20 tokens
  uint8_t dispensed;        // Actual count
  TransactionState state;   // Current state
};

#endif
