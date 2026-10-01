// firmware/dispenser/error_decoder.cpp

#include "error_decoder.h"

ErrorDecoder::ErrorDecoder()
  : state(DECODER_IDLE),
    lastFallTime(0),
    lastPulseTime(0),
    pulseCount(0),
    detectedCode(ERROR_NONE),
    newErrorReady(false),
    edgeCount(0),
    widthIndex(0),
    malformedCount(0) {
  for (uint8_t i = 0; i < DEBUG_WIDTHS; i++) lastWidthsMs[i] = 0;
}

void ErrorDecoder::begin() {
  state = DECODER_IDLE;
  pulseCount = 0;
  lastFallTime = 0;
  lastPulseTime = micros();
  detectedCode = ERROR_NONE;
  newErrorReady = false;

  Serial.println("[ErrorDecoder] Initialized - ready to decode error pulses");
}

void IRAM_ATTR ErrorDecoder::handlePinChange(bool pinState, unsigned long now) {
  // With ERROR_SIGNAL_INVERTED the pulse is the HIGH phase (line released),
  // so the edge that starts a pulse is the rising one. See config.h.
  bool pulseStart = ERROR_SIGNAL_INVERTED ? (pinState == HIGH) : (pinState == LOW);
  edgeCount++;
  if (pulseStart) {
    // pulse start
    lastFallTime = now;
  } else {
    // pulse end, measure width
    unsigned long width = (now - lastFallTime) / 1000; // convert to ms
    lastWidthsMs[widthIndex] = (uint16_t)(width > 65535 ? 65535 : width);
    widthIndex = (widthIndex + 1) % DEBUG_WIDTHS;

    if (state == DECODER_IDLE && width >= 90 && width <= 110) {
      // Valid start pulse (100ms ±10%)
      state = DECODER_START_PULSE;
      pulseCount = 0;
      lastPulseTime = now;
    } else if (state == DECODER_START_PULSE && width >= 8 && width <= 12) {
      // Valid code pulse (10ms ±20%)
      pulseCount++;
      lastPulseTime = now;
    }
  }
}

void ErrorDecoder::update() {
  if (state == DECODER_IDLE) return;

  // Read lastPulseTime atomically (multi-byte read from ISR context)
  noInterrupts();
  unsigned long elapsed = (micros() - lastPulseTime) / 1000; // ms
  interrupts();

  if (elapsed > 200) {
    // Timeout - sequence complete or malformed
    if (state == DECODER_START_PULSE && pulseCount >= 1 && pulseCount <= 7) {
      // Valid error code
      detectedCode = (ErrorCode)pulseCount;
    } else {
      // Malformed sequence (wrong pulse count or timeout in wrong state)
      Serial.print("[ErrorDecoder] malformed string, code pulses counted: ");
      Serial.println(pulseCount);
      malformedCount++;
      detectedCode = ERROR_NONE; // ERROR_UNKNOWN
    }
    newErrorReady = true;
    state = DECODER_IDLE;
  }
}

bool ErrorDecoder::hasNewError() {
  return newErrorReady;
}

ErrorCode ErrorDecoder::getErrorCode() {
  return detectedCode;
}

void ErrorDecoder::reset() {
  newErrorReady = false;
  detectedCode = ERROR_NONE;
}

const char* errorCodeToString(ErrorCode code) {
  switch (code) {
    case ERROR_COIN_STUCK: return "COIN_STUCK";
    case ERROR_SENSOR_OFF: return "SENSOR_OFF";
    case ERROR_JAM_PERMANENT: return "JAM_PERMANENT";
    case ERROR_MAX_SPAN: return "MAX_SPAN";
    case ERROR_MOTOR_FAULT: return "MOTOR_FAULT";
    case ERROR_SENSOR_FAULT: return "SENSOR_FAULT";
    case ERROR_POWER_FAULT: return "POWER_FAULT";
    default: return "UNKNOWN";
  }
}

const char* errorCodeToDescription(ErrorCode code) {
  switch (code) {
    case ERROR_COIN_STUCK: return "Coin stuck in exit sensor (>65ms)";
    case ERROR_SENSOR_OFF: return "Exit sensor stuck OFF";
    case ERROR_JAM_PERMANENT: return "Permanent jam detected";
    case ERROR_MAX_SPAN: return "Multiple spans exceeded max time";
    case ERROR_MOTOR_FAULT: return "Motor doesn't start";
    case ERROR_SENSOR_FAULT: return "Exit sensor disconnected/faulty";
    case ERROR_POWER_FAULT: return "Power supply out of range";
    default: return "Unknown or malformed error signal";
  }
}
