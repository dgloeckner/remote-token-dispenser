// firmware/dispenser/hopper_control.h

#ifndef HOPPER_CONTROL_H
#define HOPPER_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "error_decoder.h"
#include "error_history.h"
#include "interfaces.h"

class HopperControl : public IHopper {
public:
  void begin();
  void startMotor() override;
  void stopMotor() override;
  uint8_t getPulseCount() override;
  void resetPulseCount() override;
  // Falling edges rejected as noise since the last reset (issue #5).
  uint32_t getFilteredPulseCount();
  // Arm ISR-level stop: the coin-pulse ISR will write MOTOR_PIN LOW the
  // instant pulse_count reaches `count`, eliminating the up-to-10ms delay
  // between a pulse firing and the main loop() reacting.  Call this with the
  // desired quantity BEFORE startMotor().  stopMotor() clears it automatically.
  void setMotorStopAt(uint8_t count) override;
  bool checkJam() override;
  bool isHopperLow() override;

  // Self-healing: clears the active decoded hopper error (see IHopper).
  void clearActiveError() override;

  // GPIO state accessors for health endpoint
  uint8_t getCoinPulseRaw();
  bool isCoinPulseActive();
  uint8_t getErrorSignalRaw();
  bool isErrorSignalActive();
  uint8_t getHopperLowRaw();

  // Error handling
  ErrorDecoder errorDecoder;
  ErrorHistory errorHistory;
  void updateErrorDecoder();  // Call in main loop

private:
  static void IRAM_ATTR handleCoinPulse();
};

#endif
