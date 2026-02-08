// firmware/dispenser/flash_storage.h

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>

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

class FlashStorage {
public:
  void begin();
  bool hasPersistedTransaction();
  PersistedTransaction load();
  void persist(const PersistedTransaction& tx);
  void clear();
};

#endif
