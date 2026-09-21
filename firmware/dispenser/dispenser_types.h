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

// What the DEVICE is doing, as reported by GET /health (issue #6).
//
// One field, not the overlapping `status` + `dispenser` pair of protocol 1:
// the terminal used to OR the two together and had to guess which one won.
// A fault is a device-level condition that outlives the transaction it broke.
enum DeviceState {
  DEVICE_IDLE = 0,
  DEVICE_DISPENSING = 1,
  DEVICE_FAULT = 2
};

// The device-level fault: "this machine needs a human", as opposed to "the
// last transaction failed".  It lives in RAM and is cleared by a reboot and
// by nothing else (owner decision, 2026-09-20) — see dispenser-protocol.md.
enum DeviceFault {
  FAULT_NONE = 0,
  FAULT_JAM = 1,           // the 5 s jam watchdog fired
  FAULT_HOPPER_ERROR = 2   // the hopper reported a decoded error (code 1-7)
};

// Why a transaction ended in STATE_ERROR.  Reported as `error_type` next to
// `error_code`, so the terminal can tell a jam from an empty hopper from a
// sensor fault instead of seeing one undifferentiated "error".
enum TxErrorKind {
  TX_ERROR_NONE = 0,
  TX_ERROR_JAM_TIMEOUT = 1,  // no pulse for JAM_TIMEOUT_MS
  TX_ERROR_HOPPER = 2,       // a decoded hopper error; the code says which
  TX_ERROR_RESET = 3         // the device reset while this transaction ran
};

// Why a POST /dispense was answered the way it was.  The HTTP layer maps it
// to a status code; startDispense() collapses it back to a bool for the
// callers that only need "did this go wrong".
enum DispenseOutcome {
  DISPENSE_STARTED = 0,      // new transaction, motor running        → 200
  DISPENSE_IDEMPOTENT = 1,   // same tx_id and quantity as before     → 200
  DISPENSE_BUSY = 2,         // ANOTHER transaction is dispensing     → 409 busy
  DISPENSE_TX_ID_REUSED = 3, // known tx_id, different quantity       → 409 tx_id reused
  DISPENSE_FAULT = 4         // the device has a fault, only a reboot  → 409 fault
                             // clears it (issue #6)
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
  uint8_t error_kind;       // a TxErrorKind: why it ended in STATE_ERROR
  uint8_t error_code;       // the Azkoyen code 1-7 for TX_ERROR_HOPPER, else 0
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
// 3: error_kind + error_code per transaction (issue #6).  The DEVICE fault is
// deliberately NOT in here — a fault is cleared by any boot, which is the
// whole of owner decision 3, and a persisted one would survive the power cycle
// that is supposed to end it.
#define PERSIST_LAYOUT_VERSION 3

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
