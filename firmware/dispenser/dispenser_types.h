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

// Persisted transaction structure
struct PersistedTransaction {
  char tx_id[17];           // "a3f8c012" + null terminator
  uint8_t quantity;         // 1-20 tokens
  uint8_t dispensed;        // Actual count
  TransactionState state;   // Current state
};

#endif
