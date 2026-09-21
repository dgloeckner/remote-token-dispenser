// firmware/dispenser/dispense_manager.cpp

#include "dispense_manager.h"
#include "config.h"
#include "error_decoder.h"
#include "log.h"
#include <string.h>

// The protocol vocabulary (dispenser-protocol.md).  It lives next to the
// state it names, in a file the native tests compile — the HTTP layer needs
// the ESP SDK and no unit test can reach it.
const char* deviceStateToString(DeviceState state) {
  switch (state) {
    case DEVICE_IDLE: return "idle";
    case DEVICE_DISPENSING: return "dispensing";
    case DEVICE_FAULT: return "fault";
    default: return "unknown";
  }
}

const char* faultToString(DeviceFault fault) {
  switch (fault) {
    case FAULT_NONE: return "none";
    case FAULT_JAM: return "jam";
    case FAULT_HOPPER_ERROR: return "hopper_error";
    default: return "unknown";
  }
}

const char* txErrorTypeToString(uint8_t error_kind, uint8_t error_code) {
  switch (error_kind) {
    case TX_ERROR_NONE: return "NONE";
    case TX_ERROR_JAM_TIMEOUT: return "JAM_TIMEOUT";
    case TX_ERROR_RESET: return "RESET";
    case TX_ERROR_HOPPER: return errorCodeToString((ErrorCode)error_code);
    default: return "UNKNOWN";
  }
}

DispenseManager::DispenseManager(IStorage& storage, IHopper& hopper, ICountMemory& counts)
  : flashStorage(storage), hopperControl(hopper), countMemory(counts) {
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;
  active_tx.count_reliable = true;
  pending_start = false;
  settling = false;
  settling_since_ms = 0;
  fault = FAULT_NONE;
  fault_code = 0;

  memset(history, 0, sizeof(history));
  history_index = 0;

  total_dispenses = 0;
  successful_count = 0;
  jam_count = 0;
  partial_count = 0;
  crash_count = 0;
  requested_tokens = 0;
  dispensed_tokens = 0;
  overrun_tokens = 0;
}

void DispenseManager::begin() {
  PersistedRecord record;
  if (!flashStorage.load(record)) {
    // No record, a half-written one, or one from another layout.  Treated as
    // empty — never as data (issue #3).
    LOG_INFO("boot: no usable persisted record");
    return;
  }

  // The ring first: it is the answer to "did this transaction ever run?" for
  // every finished transaction, and without it a reboot turns every one of
  // them into a 404 that the terminal cannot tell from "never arrived".
  memcpy(history, record.ring, sizeof(history));
  history_index = record.ring_index % RING_BUFFER_SIZE;

  const PersistedTransaction& persisted = record.active;
  strncpy(active_tx.tx_id, persisted.tx_id, 16);
  active_tx.tx_id[16] = '\0';
  active_tx.quantity = persisted.quantity;
  active_tx.dispensed = persisted.dispensed;
  active_tx.state = (TransactionState)persisted.state;
  active_tx.count_reliable = persisted.count_reliable != 0;
  active_tx.error_kind = persisted.error_kind;
  active_tx.error_code = persisted.error_code;

  if (active_tx.state == STATE_DISPENSING) {
    // The device reset while tokens were dropping.  How many are in the tray
    // is only knowable from RTC memory: flash holds the zero written at start.
    uint8_t live_count = 0;
    if (countMemory.readCount(active_tx.tx_id, live_count)) {
      active_tx.dispensed = live_count;
      active_tx.count_reliable = true;
      LOG_INFO("boot: recovered live count %u from RTC memory", (unsigned)live_count);
    } else {
      // A real power loss took the RTC block with it.  What is left is a lower
      // bound, and saying so is the whole point: the terminal bills the bound
      // and flags the rest for a human, instead of billing nothing.
      active_tx.count_reliable = false;
      LOG_INFO("boot: RTC count lost (power loss), count is a lower bound");
    }

    active_tx.state = STATE_ERROR;
    active_tx.error_kind = TX_ERROR_RESET;
    active_tx.error_code = 0;

    // Count the crashed transaction
    total_dispenses++;                        // Transaction was started before the reset
    crash_count++;                            // Track crashes specifically
    requested_tokens += active_tx.quantity;
    dispensed_tokens += active_tx.dispensed;

    addToHistory(active_tx);
    countMemory.invalidate();

    LOG_INFO("boot: %s recovered as ERROR after a reset, dispensed %u",
             active_tx.tx_id, (unsigned)active_tx.dispensed);

    // The device itself is fine, and it says so: a recovered crash sets NO
    // fault (issue #6).  Leaving the transaction in the active slot was what
    // took the machine out of service over a watchdog reset — the terminal
    // read "error", greyed the tokens out, and the dispense that would have
    // cleared the state was exactly the one nobody could start.
    memset(&active_tx, 0, sizeof(active_tx));
    active_tx.state = STATE_IDLE;
    active_tx.count_reliable = true;
    persistState();
  } else if (active_tx.state == STATE_ERROR) {
    // Power cycled to clear a jam — the documented manual reset.  Only the
    // ACTIVE slot is cleared: the ring stays, so the transaction that jammed
    // can still be asked about.  (Nothing is added to the ring here; the
    // transition that ended it already did, and a second boot used to plant an
    // entry made of zeroes.)
    LOG_INFO("boot: previous error cleared (manual reset via power cycle)");
    memset(&active_tx, 0, sizeof(active_tx));
    active_tx.state = STATE_IDLE;
    active_tx.count_reliable = true;
    persistState();
  }
}

DispenseOutcome DispenseManager::requestDispense(const char* tx_id, uint8_t quantity) {
  LOG_DEBUG("requestDispense tx_id=%s quantity=%u", tx_id, (unsigned)quantity);

  // Idempotency, first half: the transaction that is running right now.  It is
  // not in the history ring yet — entries land there when a transaction
  // finishes — so it has to be recognised here, or the busy check below would
  // answer 409 to the caller's own retry while its tokens are falling.
  if (active_tx.state != STATE_IDLE && strcmp(active_tx.tx_id, tx_id) == 0) {
    if (active_tx.quantity != quantity) {
      LOG_ERROR("tx_id %s of the active transaction reused with another quantity", tx_id);
      return DISPENSE_TX_ID_REUSED;
    }
    LOG_DEBUG("retry of the active transaction %s (idempotent)", tx_id);
    return DISPENSE_IDEMPOTENT;
  }

  // Idempotency, second half: a finished transaction in the ring.  active_tx
  // is deliberately NOT touched here.  Overwriting it used to switch loop()
  // off while the motor was running (no jam watchdog), and to make an idle
  // device report the state of some old transaction in /health.
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    if (cached_tx.quantity != quantity) {
      LOG_ERROR("known tx_id %s reused with another quantity", tx_id);
      return DISPENSE_TX_ID_REUSED;
    }
    LOG_DEBUG("transaction %s found in history (idempotent)", tx_id);
    return DISPENSE_IDEMPOTENT;
  }

  // Check if busy.  From here on a 409 always means ANOTHER transaction.
  if (active_tx.state == STATE_DISPENSING) {
    LOG_INFO("busy: %s is dispensing, %s rejected", active_tx.tx_id, tx_id);
    return DISPENSE_BUSY;
  }

  // The device fault, and it comes after the two idempotency checks on
  // purpose: a terminal asking what became of a transaction it already sent
  // gets its answer even now — it is asking about tokens that already fell,
  // not for new ones.  A NEW transaction is refused until somebody pulls the
  // plug (owner decision 3, 2026-09-20).
  if (fault != FAULT_NONE) {
    LOG_INFO("fault %s: %s rejected", faultToString(fault), tx_id);
    return DISPENSE_FAULT;
  }

  // Accept the transaction — in memory only.  This call runs in the async TCP
  // callback, where a flash sector erase and a blocking serial write starve the
  // WiFi stack and make the POST take seconds (issue #4).  The state goes to
  // DISPENSING right here, so the answer, the busy check and GET /dispense all
  // see it; the work goes into the slot for the next loop() pass, at most 10 ms
  // later.
  //
  // A reset in that window loses the transaction, because nothing was written
  // yet — and that is the harmless direction: no token has dropped, so the
  // terminal's retry starts it for real.
  LOG_INFO("accepted %s, quantity %u", tx_id, (unsigned)quantity);
  strncpy(active_tx.tx_id, tx_id, 16);
  active_tx.tx_id[16] = '\0';
  active_tx.quantity = quantity;
  active_tx.dispensed = 0;
  active_tx.state = STATE_DISPENSING;
  active_tx.count_reliable = true;
  active_tx.started_ms = millis();
  pending_start = true;

  // Update metrics
  total_dispenses++;
  requested_tokens += quantity;

  return DISPENSE_STARTED;
}

void DispenseManager::startPending() {
  // The slot is cleared FIRST: whatever happens below, this transaction is
  // never started twice.
  pending_start = false;
  settling = false;

  LOG_INFO("starting %s", active_tx.tx_id);
  persistState();

  // Seed the live count, so a reset before the first token recovers a
  // reliable zero rather than "no block, count unknown".
  countMemory.writeCount(active_tx.tx_id, 0);

  // Arm ISR-level stop BEFORE starting motor so the very first pulse that
  // reaches the target immediately cuts motor power, eliminating the ~10ms
  // main-loop latency that was causing occasional double-dispenses.
  hopperControl.resetPulseCount();
  hopperControl.setMotorStopAt(active_tx.quantity);
  hopperControl.startMotor();
}

void DispenseManager::finishDispense() {
  settling = false;

  // What actually came out.  It may be MORE than was asked for: the tokens
  // that fell while the disc coasted are in the tray either way, and the
  // terminal bills `dispensed` (dispenser-protocol.md).  Reporting the
  // requested quantity instead was the silent half of issue #5.
  if (active_tx.dispensed > active_tx.quantity) {
    uint8_t overrun = active_tx.dispensed - active_tx.quantity;
    overrun_tokens += overrun;
    LOG_INFO("overrun: %s delivered %u for a quantity of %u", active_tx.tx_id,
             (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);
  }

  LOG_INFO("done: %s dispensed %u/%u", active_tx.tx_id,
           (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);
  active_tx.state = STATE_DONE;

  // Track dispensed tokens
  dispensed_tokens += active_tx.dispensed;

  addToHistory(active_tx);
  countMemory.invalidate();

  // One commit, not two and an erase: the finished transaction goes into the
  // ring and the active slot goes empty in the same write.  Clearing the
  // record here is what made a completed transaction a 404 after a reboot.
  // The settling window adds no commit of its own — it is not a state
  // transition, it is the end of one (owner decision, 2026-09-20).
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;
  active_tx.count_reliable = true;
  persistState();
  successful_count++;
}

bool DispenseManager::startDispense(const char* tx_id, uint8_t quantity) {
  DispenseOutcome outcome = requestDispense(tx_id, quantity);
  return outcome == DISPENSE_STARTED || outcome == DISPENSE_IDEMPOTENT;
}

void DispenseManager::loop() {
  if (pending_start) {
    // A transaction the HTTP layer accepted in the TCP callback.  Commit it and
    // start the motor here, in loop() context, then leave: a motor that has
    // just started has dropped no token and cannot have jammed, so the
    // monitoring below has nothing to do until the next pass.
    startPending();
    return;
  }

  // The hopper's own verdict, ahead of everything the firmware infers.  It
  // is checked even while idle: "the hopper is jammed" does not stop being
  // true because nothing is running, and the next POST must be refused
  // rather than driving a motor into it (issue #6).
  uint8_t decoded = hopperControl.takeDecodedError();
  if (decoded != 0) {
    raiseFault(FAULT_HOPPER_ERROR, decoded, TX_ERROR_HOPPER);
    return;
  }

  if (active_tx.state != STATE_DISPENSING) {
    return;  // Nothing to monitor
  }

  // Update dispensed count from pulse counter
  uint8_t previous_count = active_tx.dispensed;
  active_tx.dispensed = hopperControl.getPulseCount();

  // Every token goes into RTC memory: it survives a watchdog reset, an
  // exception and a brownout, and costs no flash wear.  A commit per token was
  // considered and rejected (owner decision, 2026-09-20) — one extra sector
  // erase per token, to cover only the power-loss case that count_reliable
  // already reports honestly.
  if (active_tx.dispensed != previous_count) {
    countMemory.writeCount(active_tx.tx_id, active_tx.dispensed);

    LOG_DEBUG("pulse %u/%u", (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);
  }

  // The settling window (issue #5).  Started by the pass below, it is checked
  // BEFORE the jam watchdog: the motor is already off in here, so `no pulse
  // for five seconds` would eventually be read as a jam on a transaction that
  // has done everything right.
  if (settling) {
    if (millis() - settling_since_ms < DISPENSE_SETTLING_MS) {
      return;  // a token may still be falling; the loop above counts it
    }
    finishDispense();
    return;
  }

  // Target reached: cut the motor, then wait out the coast.  The ISR has
  // normally cut it already — this is the belt to that braces, and it is also
  // what clears the ISR stop target.
  if (active_tx.dispensed >= active_tx.quantity) {
    hopperControl.stopMotor();
    settling = true;
    settling_since_ms = millis();
    LOG_INFO("target reached: %s at %u/%u, settling", active_tx.tx_id,
             (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);
    return;
  }

  // Check for jam
  if (hopperControl.checkJam()) {
    LOG_ERROR("jam: %s stopped at %u/%u",
              active_tx.tx_id, (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);
    jam_count++;
    raiseFault(FAULT_JAM, 0, TX_ERROR_JAM_TIMEOUT);
    return;
  }
}

// The device needs a human.  Only a reboot ends this (owner decision 3,
// 2026-09-20): there is no reset route, no button in the TUI and none on the
// kiosk, and the fault is deliberately not persisted — any boot clears it, and
// a jam that is still there simply faults the next dispense again, having
// dispensed and billed nothing.
void DispenseManager::raiseFault(DeviceFault which, uint8_t code, uint8_t error_kind) {
  if (fault == FAULT_NONE) {
    fault = which;
    fault_code = code;
    LOG_ERROR("fault %s (code %u): the device is out of service until a power cycle",
              faultToString(which), (unsigned)code);
  }

  if (active_tx.state == STATE_DISPENSING) {
    failActive(error_kind, code);
  }
}

void DispenseManager::failActive(uint8_t error_kind, uint8_t error_code) {
  // The motor first, before any log call: on this path it is time the motor
  // is still running (issue #4).
  hopperControl.stopMotor();
  // The settling window is over, whatever it was waiting for.  A flag left
  // standing here would let the next loop() pass finish a failed transaction
  // as `done` once the 500 ms are up (issue #5).
  settling = false;

  active_tx.state = STATE_ERROR;
  active_tx.error_kind = error_kind;
  active_tx.error_code = error_code;

  LOG_ERROR("%s failed as %s at %u/%u", active_tx.tx_id,
            txErrorTypeToString(error_kind, error_code),
            (unsigned)active_tx.dispensed, (unsigned)active_tx.quantity);

  // The tokens that did fall are in the tray and are billed.
  dispensed_tokens += active_tx.dispensed;
  if (active_tx.dispensed > 0) {
    partial_count++;
  }

  addToHistory(active_tx);
  countMemory.invalidate();

  // One commit, and the active slot goes empty with it: the failed
  // transaction lives in the ring, where a GET still finds it, and the device
  // state is the fault — not a transaction nobody can clear.
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;
  active_tx.count_reliable = true;
  persistState();
}

Transaction DispenseManager::getTransaction(const char* tx_id) {
  // Check active transaction
  if (strcmp(active_tx.tx_id, tx_id) == 0) {
    return active_tx;
  }

  // Check history
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    return cached_tx;
  }

  // Not found - return empty with IDLE state
  Transaction empty_tx;
  memset(&empty_tx, 0, sizeof(empty_tx));
  empty_tx.state = STATE_IDLE;
  empty_tx.count_reliable = true;
  empty_tx.error_kind = TX_ERROR_NONE;
  return empty_tx;
}

Transaction DispenseManager::getActiveTransaction() {
  return active_tx;
}

bool DispenseManager::isIdle() {
  // A faulted device is not idle, whatever the transaction slot says.  Before
  // #6 this asked only about STATE_DISPENSING, which is the whole bug: an
  // active error let the next POST straight through.
  return fault == FAULT_NONE && active_tx.state != STATE_DISPENSING;
}

// The device as GET /health reports it (issue #6).
DeviceState DispenseManager::getDeviceState() {
  if (fault != FAULT_NONE) {
    return DEVICE_FAULT;
  }
  if (active_tx.state == STATE_DISPENSING) {
    return DEVICE_DISPENSING;
  }
  return DEVICE_IDLE;
}

DeviceFault DispenseManager::getFault() { return fault; }
uint8_t DispenseManager::getFaultCode() { return fault_code; }

uint16_t DispenseManager::getTotalDispenses() { return total_dispenses; }
uint16_t DispenseManager::getSuccessful() { return successful_count; }
uint16_t DispenseManager::getJams() { return jam_count; }
uint16_t DispenseManager::getPartial() { return partial_count; }
uint16_t DispenseManager::getCrashes() { return crash_count; }
uint32_t DispenseManager::getRequestedTokens() { return requested_tokens; }
uint32_t DispenseManager::getDispensedTokens() { return dispensed_tokens; }
uint32_t DispenseManager::getOverrunTokens() { return overrun_tokens; }

// Private methods
bool DispenseManager::findInHistory(const char* tx_id, Transaction& out_tx) {
  if (tx_id == NULL || tx_id[0] == '\0') {
    return false;
  }
  for (int i = 0; i < RING_BUFFER_SIZE; i++) {
    if (strcmp(history[i].tx_id, tx_id) == 0) {
      // Found in history - return complete transaction data
      memset(&out_tx, 0, sizeof(out_tx));
      strncpy(out_tx.tx_id, tx_id, 16);
      out_tx.tx_id[16] = '\0';
      out_tx.state = (TransactionState)history[i].state;
      out_tx.quantity = history[i].quantity;
      out_tx.dispensed = history[i].dispensed;
      out_tx.count_reliable = history[i].count_reliable != 0;
      out_tx.error_kind = history[i].error_kind;
      out_tx.error_code = history[i].error_code;
      return true;
    }
  }
  return false;
}

void DispenseManager::addToHistory(const Transaction& tx) {
  strncpy(history[history_index].tx_id, tx.tx_id, 16);
  history[history_index].tx_id[16] = '\0';
  history[history_index].state = (uint8_t)tx.state;
  history[history_index].quantity = tx.quantity;
  history[history_index].dispensed = tx.dispensed;
  history[history_index].count_reliable = tx.count_reliable ? 1 : 0;
  history[history_index].error_kind = tx.error_kind;
  history[history_index].error_code = tx.error_code;
  history_index = (history_index + 1) % RING_BUFFER_SIZE;
}

void DispenseManager::persistState() {
  PersistedRecord record;
  memset(&record, 0, sizeof(record));

  strncpy(record.active.tx_id, active_tx.tx_id, 16);
  record.active.tx_id[16] = '\0';
  record.active.quantity = active_tx.quantity;
  record.active.dispensed = active_tx.dispensed;
  record.active.state = (uint8_t)active_tx.state;
  record.active.count_reliable = active_tx.count_reliable ? 1 : 0;
  record.active.error_kind = active_tx.error_kind;
  record.active.error_code = active_tx.error_code;

  memcpy(record.ring, history, sizeof(record.ring));
  record.ring_index = history_index;

  flashStorage.save(record);
}
