// firmware/dispenser/hopper_control.h

#ifndef HOPPER_CONTROL_H
#define HOPPER_CONTROL_H

#include <Arduino.h>
#include "config.h"

class HopperControl {
public:
  void begin();
  void startMotor();
  void stopMotor();
  uint8_t getPulseCount();
  void resetPulseCount();
  bool checkJam();
  bool isHopperLow();

private:
  static void IRAM_ATTR handleCoinPulse();
};

#endif
