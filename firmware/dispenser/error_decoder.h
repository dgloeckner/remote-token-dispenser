// firmware/dispenser/error_decoder.h

#ifndef ERROR_DECODER_H
#define ERROR_DECODER_H

#include <Arduino.h>
#include "config.h"

// Error codes from Azkoyen Hopper U-II protocol
// See docs/azkoyen-hopper-protocol.md section 3.5
enum ErrorCode {
  ERROR_NONE = 0,           // No error / Unknown (malformed signal)
  ERROR_COIN_STUCK = 1,     // Coin exit sensor > 65ms
  ERROR_SENSOR_OFF = 2,     // Exit sensor stuck OFF
  ERROR_JAM_PERMANENT = 3,  // Permanent jam detected
  ERROR_MAX_SPAN = 4,       // Multiple spans > max time
  ERROR_MOTOR_FAULT = 5,    // Motor doesn't start
  ERROR_SENSOR_FAULT = 6,   // Exit sensor disconnected
  ERROR_POWER_FAULT = 7     // Power supply out of range
};

// State machine states for pulse decoding
enum DecoderState {
  STATE_IDLE,        // Waiting for error signal (pin HIGH)
  STATE_START_PULSE  // First LOW detected, counting code pulses
};

class ErrorDecoder {
private:
  volatile DecoderState state;
  volatile unsigned long lastFallTime;  // micros() when pin went LOW
  volatile unsigned long lastPulseTime; // micros() when last pulse ended
  volatile uint8_t pulseCount;          // Number of code pulses counted
  volatile ErrorCode detectedCode;
  volatile bool newErrorReady;

public:
  ErrorDecoder();
  void begin();
  void update();  // Call in main loop - checks timeout, finalizes error code
  void IRAM_ATTR handlePinChange(bool pinState, unsigned long now);  // Called from ISR
  bool hasNewError();
  ErrorCode getErrorCode();
  void reset();
};

// Helper functions for error code conversion
const char* errorCodeToString(ErrorCode code);
const char* errorCodeToDescription(ErrorCode code);

#endif
