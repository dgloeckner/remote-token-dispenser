// firmware/dispenser/dispenser_types.h
//
// Plain data shared by the production classes and by the native unit tests.
// Deliberately free of ESP SDK includes so that the native test build can
// compile the real production code (see firmware/dispenser/test/README.md).

#ifndef DISPENSER_TYPES_H
#define DISPENSER_TYPES_H

#include <stdint.h>
#include <stddef.h>

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

// One transaction as it is written to flash.
//
// `state` is a uint8_t and not the enum: the enum is 4 bytes wide on the ESP
// and 4 bytes wide on the host today, but nothing in the language promises
// that, and this struct is a wire format between two builds of the firmware.
struct PersistedTransaction {
  char tx_id[17];           // "a3f8c012" + null terminator
  uint8_t quantity;         // 1-20 tokens
  uint8_t dispensed;        // Actual count
  uint8_t state;            // a TransactionState, stored as one byte
  uint8_t count_reliable;   // 1 = `dispensed` is exact, 0 = lower bound only
};

#define PERSIST_RING_SIZE 8

// Everything the firmware must find again after a reboot: the transaction that
// was running, AND the ring of finished ones.  The ring used to live in RAM
// only, so every completed transaction answered 404 after a reboot — and a 404
// cannot be told apart from "the request never arrived" (issue #3).
//
// magic + layout_version + crc16 are the three guards.  Anything that fails one
// of them is treated as EMPTY, never as data: a half-written record, a record
// from a development build, or the bytes an older layout left behind.
#define PERSIST_MAGIC 0x46335458UL   // "F3TX" — bumped with the layout of #3
#define PERSIST_LAYOUT_VERSION 2

struct PersistedRecord {
  uint32_t magic;
  uint16_t layout_version;
  uint16_t crc16;                            // over the record with this field zeroed
  PersistedTransaction active;               // STATE_IDLE == no active transaction
  PersistedTransaction ring[PERSIST_RING_SIZE];
  uint8_t ring_index;                        // next slot to write
  uint8_t reserved[3];                       // keep the layout explicit, not padded
};

// The live token count, in RTC user memory.
//
// RTC memory survives a watchdog reset, an exception and a soft reset — the
// resets a motor on a shared supply actually causes — and costs no flash wear,
// so the count can be written on every single token.  It does NOT survive a
// real power loss; that case is reported as count_reliable = false rather than
// silently as zero.  (Owner decision, 2026-09-20: no flash commit per token.)
#define RTC_COUNT_MAGIC 0x46335243UL   // "F3RC"

struct RtcCountBlock {
  uint32_t magic;
  char tx_id[20];      // 17 rounded up: rtcUserMemoryWrite works in 4-byte words
  uint32_t dispensed;
  uint32_t crc;        // over the block with this field zeroed
};

#endif
