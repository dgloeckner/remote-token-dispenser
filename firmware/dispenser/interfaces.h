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

// Persistence of the active transaction (EEPROM on the device).
class IStorage {
public:
  virtual ~IStorage() {}
  virtual void begin() = 0;
  virtual bool hasPersistedTransaction() = 0;
  virtual PersistedTransaction load() = 0;
  virtual void persist(const PersistedTransaction& tx) = 0;
  virtual void clear() = 0;
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
