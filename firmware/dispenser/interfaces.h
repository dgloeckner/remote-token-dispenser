// firmware/dispenser/interfaces.h
//
// The two collaborators of DispenseManager, as interfaces.
//
// Why: the native unit tests must exercise the *production* DispenseManager.
// Before #1 the test suite re-declared a copy of the class against the mocks,
// so a bug in dispense_manager.cpp could neither be turned red nor green by a
// test.  With these interfaces the production class takes the mocks directly
// and dispense_manager.cpp is linked into the test binary.
//
// Keep this header free of Arduino/ESP SDK includes.

#ifndef INTERFACES_H
#define INTERFACES_H

#include <stdint.h>
#include "dispenser_types.h"

// Persistence of the crash-safe record (EEPROM on the device): the active
// transaction AND the ring of finished ones.  It is one record and one commit,
// because the ring has to survive a reboot without costing an extra sector
// erase (issue #3).
class IStorage {
public:
  virtual ~IStorage() {}
  virtual void begin() = 0;
  // Reads the stored record.  false means "nothing usable": no record, a
  // half-written one, or one from another layout.  `out` is then untouched.
  virtual bool load(PersistedRecord& out) = 0;
  virtual void save(const PersistedRecord& record) = 0;
  virtual void clear() = 0;
};

// The live token count between two state transitions.
//
// On the device this is RTC user memory: it survives a watchdog reset, an
// exception and a soft reset, so the count may be written on every token
// without a flash erase.  A real power loss wipes it — readCount() then says
// no, and the transaction is reported with count_reliable = false instead of
// with a fabricated zero.
class ICountMemory {
public:
  virtual ~ICountMemory() {}
  // true only when an intact count for THIS tx_id is present.
  virtual bool readCount(const char* tx_id, uint8_t& out_count) = 0;
  virtual void writeCount(const char* tx_id, uint8_t count) = 0;
  virtual void invalidate() = 0;
};

// Everything DispenseManager needs from the hopper hardware.
// Note: the error *history* is reached through clearActiveError() rather than
// by exposing the ErrorHistory object, so this interface stays data-only.
class IHopper {
public:
  virtual ~IHopper() {}
  virtual void startMotor() = 0;
  virtual void stopMotor() = 0;
  virtual uint8_t getPulseCount() = 0;
  virtual void resetPulseCount() = 0;
  // Arm the ISR-level stop: the coin-pulse ISR cuts motor power the instant
  // the pulse count reaches `count`.  Call before startMotor().
  virtual void setMotorStopAt(uint8_t count) = 0;
  virtual bool checkJam() = 0;
  virtual bool isHopperLow() = 0;
  // Self-healing: a completed dispense clears the active hopper error.
  virtual void clearActiveError() = 0;
};

#endif
