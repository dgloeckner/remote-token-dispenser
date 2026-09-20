// Test double for FlashStorage — implements the production IStorage interface,
// so the production DispenseManager takes it without any code copy.
//
// It keeps the record as RAW BYTES, exactly as the EEPROM does, and runs the
// production validity check on load().  That is what makes "a corrupt or
// older record is ignored" testable at all: a test can flip a byte or the
// layout version and see the firmware treat the record as empty.
#ifndef FLASH_STORAGE_MOCK_H
#define FLASH_STORAGE_MOCK_H

#include <stdint.h>
#include <string.h>

#include "crash_state.h"
#include "interfaces.h"

class FlashStorageMock : public IStorage {
private:
    bool present;
    uint8_t bytes[sizeof(PersistedRecord)];
    int save_calls;
    int clear_calls;

public:
    FlashStorageMock() : present(false), save_calls(0), clear_calls(0) {
        memset(bytes, 0, sizeof(bytes));
    }

    void begin() override {}

    bool load(PersistedRecord& out) override {
        if (!present) {
            return false;
        }
        PersistedRecord stored;
        memcpy(&stored, bytes, sizeof(stored));
        if (!persistedRecordValid(stored)) {
            return false;
        }
        out = stored;
        return true;
    }

    void save(const PersistedRecord& record) override {
        PersistedRecord sealed = record;
        sealPersistedRecord(sealed);
        memcpy(bytes, &sealed, sizeof(sealed));
        present = true;
        save_calls++;
    }

    void clear() override {
        present = false;
        memset(bytes, 0, sizeof(bytes));
        clear_calls++;
    }

    // ---- test helpers -----------------------------------------------------

    // Seed the record a reboot would find: one active transaction, no history.
    void setPersistedTransaction(const char* tx_id, uint8_t quantity,
                                 uint8_t dispensed, TransactionState state) {
        PersistedRecord record;
        memcpy(&record, bytes, sizeof(record));
        if (!present) {
            memset(&record, 0, sizeof(record));
        }
        strncpy(record.active.tx_id, tx_id, 16);
        record.active.tx_id[16] = '\0';
        record.active.quantity = quantity;
        record.active.dispensed = dispensed;
        record.active.state = (uint8_t)state;
        record.active.count_reliable = 1;
        sealPersistedRecord(record);
        memcpy(bytes, &record, sizeof(record));
        present = true;
    }

    // Read back what the firmware wrote, without the validity check.
    PersistedRecord raw() const {
        PersistedRecord record;
        memcpy(&record, bytes, sizeof(record));
        return record;
    }

    bool hasRecord() const { return present; }

    // Damage the stored record the way a half-finished commit would.
    void corruptOneByte() {
        bytes[sizeof(PersistedRecord) / 2] ^= 0xFF;
    }

    // Pretend the bytes were written by a build with another layout.
    void setStoredLayoutVersion(uint16_t version) {
        PersistedRecord record;
        memcpy(&record, bytes, sizeof(record));
        record.layout_version = version;
        memcpy(bytes, &record, sizeof(record));
    }

    int getSaveCalls() const { return save_calls; }
    int getClearCalls() const { return clear_calls; }
    void resetCallCounts() { save_calls = 0; clear_calls = 0; }
};

#endif // FLASH_STORAGE_MOCK_H
