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
  // Why this transaction ended in STATE_ERROR (issue #6): a TxErrorKind and,
  // for TX_ERROR_HOPPER, the Azkoyen code 1-7.  Reported as `error_type` /
  // `error_code` on every transaction response, so the terminal can tell a
  // jam from a motor fault instead of seeing one flat "error".
  uint8_t error_kind;
  uint8_t error_code;
  unsigned long started_ms;
};

// The protocol vocabulary for the three fields above.  They live here, in a
// file the native tests compile, and not in the HTTP layer, which no unit test
// can reach.
const char* deviceStateToString(DeviceState state);
const char* faultToString(DeviceFault fault);
const char* txErrorTypeToString(uint8_t error_kind, uint8_t error_code);

class DispenseManager {
public:
  DispenseManager(IStorage& storage, IHopper& hopper, ICountMemory& countMemory);

  void begin();
  // Called from the main loop: starts a request the HTTP layer accepted, then
  // watches the motor.  Everything with a side effect happens here — see
  // `pending_start` below.
  void loop();

  // Transaction operations.
  //
  // requestDispense() is the full answer (see DispenseOutcome).  It is safe to
  // call from the async TCP callback: it decides in memory and touches neither
  // flash, nor RTC memory, nor the motor (issue #4).  The next loop() pass
  // does that work.  startDispense() is the same call reduced to
  // "accepted or not".
  DispenseOutcome requestDispense(const char* tx_id, uint8_t quantity);
  bool startDispense(const char* tx_id, uint8_t quantity);
  Transaction getTransaction(const char* tx_id);
  Transaction getActiveTransaction();
  bool isIdle();

  // The device, as GET /health reports it (issue #6).  A fault outranks
  // everything: it is the answer to "is it safe to run the motor", and only a
  // reboot changes it.
  DeviceState getDeviceState();
  DeviceFault getFault();
  // The Azkoyen code behind a FAULT_HOPPER_ERROR, 0 otherwise.
  uint8_t getFaultCode();

  // Transaction-level metrics
  uint16_t getTotalDispenses();
  uint16_t getSuccessful();
  uint16_t getJams();
  uint16_t getPartial();
  uint16_t getCrashes();

  // Token-level metrics
  uint32_t getRequestedTokens();
  uint32_t getDispensedTokens();
  // Tokens that left the hopper past the requested quantity (issue #5): the
  // ones that fall while the disc coasts to a stop.  They used to be counted
  // by the ISR and then dropped on the floor of the accounting, because
  // `dispensed` was clamped at `quantity` by construction.
  uint32_t getOverrunTokens();

private:
  IStorage& flashStorage;
  IHopper& hopperControl;
  ICountMemory& countMemory;

  Transaction active_tx;

  // The request slot: set by requestDispense() when a new transaction is
  // accepted, cleared by the loop() pass that commits it and starts the motor.
  // One slot, not a queue — the device runs exactly one transaction at a time,
  // so a second request while this is set is the same 409 busy it always was.
  bool pending_start;

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
  uint32_t overrun_tokens;

  bool findInHistory(const char* tx_id, Transaction& out_tx);
  void addToHistory(const Transaction& tx);
  // Writes the active transaction AND the ring in one commit.
  void persistState();
  // Consumes the request slot: commit, seed the live count, start the motor.
  void startPending();
  // Ends the transaction after the settling window: the final count, the
  // overrun, the ring, one commit.
  void finishDispense();

  // The settling window (issue #5).  The motor is cut by the ISR the instant
  // the target count is reached, but a token already past the wheel still
  // falls.  The transaction therefore stays DISPENSING for
  // DISPENSE_SETTLING_MS after the stop and keeps counting; only then is the
  // count final.  It is not a new protocol state: the device reports
  // `dispensing` throughout, which is what it is doing — finishing.
  bool settling;
  unsigned long settling_since_ms;

  // The device-level fault.  RAM only, on purpose: a boot clears it, which is
  // the entire reset story (owner decision 3, 2026-09-20).  Persisting it
  // would make the power cycle that is supposed to end a jam the one thing
  // that cannot.
  DeviceFault fault;
  uint8_t fault_code;

  // Raise the device fault and, if one is running, end the transaction with
  // it.  The only callers are the jam watchdog and the decoded hopper error.
  void raiseFault(DeviceFault which, uint8_t code, uint8_t error_kind);
  // End the active transaction in STATE_ERROR: motor off, settling window
  // closed, metrics, ring, one commit, active slot empty.
  void failActive(uint8_t error_kind, uint8_t error_code);

};

#endif
