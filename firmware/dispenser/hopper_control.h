// firmware/dispenser/hopper_control.h

#ifndef HOPPER_CONTROL_H
#define HOPPER_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "error_decoder.h"
#include "error_history.h"

class HopperControl {
public:
  void begin();
  void startMotor();
  void stopMotor();
  uint8_t getPulseCount();
  void resetPulseCount();
  bool checkJam();
  bool isHopperLow();

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
