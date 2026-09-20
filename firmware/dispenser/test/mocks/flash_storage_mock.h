// Test double for FlashStorage — implements the production IStorage interface,
// so the production DispenseManager takes it without any code copy.
#ifndef FLASH_STORAGE_MOCK_H
#define FLASH_STORAGE_MOCK_H

#include <stdint.h>
#include <string.h>

#include "interfaces.h"

class FlashStorageMock : public IStorage {
private:
    bool has_transaction;
    PersistedTransaction stored_tx;
    int persist_calls;
    int clear_calls;

public:
    FlashStorageMock() : has_transaction(false), persist_calls(0), clear_calls(0) {
        memset(&stored_tx, 0, sizeof(stored_tx));
    }

    void begin() override {}

    bool hasPersistedTransaction() override {
        return has_transaction;
    }

    PersistedTransaction load() override {
        return stored_tx;
    }

    void persist(const PersistedTransaction& tx) override {
        stored_tx = tx;
        has_transaction = true;
        persist_calls++;
    }

    void clear() override {
        has_transaction = false;
        memset(&stored_tx, 0, sizeof(stored_tx));
        clear_calls++;
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

    int getPersistCalls() const { return persist_calls; }
    int getClearCalls() const { return clear_calls; }
    void resetCallCounts() { persist_calls = 0; clear_calls = 0; }
};

#endif // FLASH_STORAGE_MOCK_H
