// Test double for RtcCountMemory — implements the production ICountMemory.
//
// The two resets this issue is about are two calls on this mock:
//   loseRtcMemory()  — a real power loss: the block is gone
//   (do nothing)     — a watchdog reset, an exception, a brownout that the
//                      RTC domain survives: the block is still there
#ifndef COUNT_MEMORY_MOCK_H
#define COUNT_MEMORY_MOCK_H

#include <stdint.h>
#include <string.h>

#include "interfaces.h"

class CountMemoryMock : public ICountMemory {
private:
    bool valid;
    char tx_id[17];
    uint8_t count;
    int write_calls;
    int invalidate_calls;

public:
    CountMemoryMock() : valid(false), count(0), write_calls(0), invalidate_calls(0) {
        tx_id[0] = '\0';
    }

    bool readCount(const char* id, uint8_t& out_count) override {
        if (!valid || id == NULL || strcmp(tx_id, id) != 0) {
            return false;
        }
        out_count = count;
        return true;
    }

    void writeCount(const char* id, uint8_t value) override {
        strncpy(tx_id, id, 16);
        tx_id[16] = '\0';
        count = value;
        valid = true;
        write_calls++;
    }

    void invalidate() override {
        valid = false;
        tx_id[0] = '\0';
        count = 0;
        invalidate_calls++;
    }

    // ---- test helpers -----------------------------------------------------

    // A power loss: the RTC domain lost its supply, the block is gone.
    void loseRtcMemory() {
        valid = false;
        tx_id[0] = '\0';
        count = 0;
    }

    // A reset the RTC domain survived, with `value` tokens already in the tray.
    void survivesWith(const char* id, uint8_t value) {
        strncpy(tx_id, id, 16);
        tx_id[16] = '\0';
        count = value;
        valid = true;
    }

    bool isValid() const { return valid; }
    uint8_t storedCount() const { return count; }
    int getWriteCalls() const { return write_calls; }
    int getInvalidateCalls() const { return invalidate_calls; }
};

#endif // COUNT_MEMORY_MOCK_H
