// Mock FlashStorage for testing
#ifndef FLASH_STORAGE_MOCK_H
#define FLASH_STORAGE_MOCK_H

#include <stdint.h>
#include <string.h>

// Transaction state enum
enum TransactionState {
  STATE_IDLE = 0,
  STATE_DISPENSING = 1,
  STATE_DONE = 2,
  STATE_ERROR = 3
};

// Persisted transaction structure
struct PersistedTransaction {
  char tx_id[17];
  uint8_t quantity;
  uint8_t dispensed;
  TransactionState state;
};

class FlashStorageMock {
private:
    bool has_transaction;
    PersistedTransaction stored_tx;

public:
    FlashStorageMock() : has_transaction(false) {
        memset(&stored_tx, 0, sizeof(stored_tx));
    }

    void begin() {}

    bool hasPersistedTransaction() {
        return has_transaction;
    }

    PersistedTransaction load() {
        return stored_tx;
    }

    void persist(const PersistedTransaction& tx) {
        stored_tx = tx;
        has_transaction = true;
    }

    void clear() {
        has_transaction = false;
        memset(&stored_tx, 0, sizeof(stored_tx));
    }

    // Test helper to set up persisted state
    void setPersistedTransaction(const char* tx_id, uint8_t quantity,
                                  uint8_t dispensed, TransactionState state) {
        strncpy(stored_tx.tx_id, tx_id, 16);
        stored_tx.tx_id[16] = '\0';
        stored_tx.quantity = quantity;
        stored_tx.dispensed = dispensed;
        stored_tx.state = state;
        has_transaction = true;
    }
};

#endif // FLASH_STORAGE_MOCK_H
