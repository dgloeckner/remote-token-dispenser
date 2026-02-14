// firmware/dispenser/error_history.cpp

#include "error_history.h"

ErrorHistory::ErrorHistory() : writeIndex(0) {
  // Initialize buffer with ERROR_NONE
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = {ERROR_NONE, 0, true};
  }
}

void ErrorHistory::addError(ErrorCode code) {
  buffer[writeIndex] = {code, millis(), false};
  writeIndex = (writeIndex + 1) % BUFFER_SIZE;

  Serial.print("[ErrorHistory] Added error: ");
  Serial.print(errorCodeToString(code));
  Serial.print(" at timestamp ");
  Serial.println(millis());
}

ErrorRecord* ErrorHistory::getActive() {
  // Search newest to oldest for first non-cleared error
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    int idx = (writeIndex + BUFFER_SIZE - 1 - i) % BUFFER_SIZE;
    if (buffer[idx].code != ERROR_NONE && !buffer[idx].cleared) {
      return &buffer[idx];
    }
  }
  return nullptr;
}

void ErrorHistory::clearActive() {
  ErrorRecord* active = getActive();
  if (active) {
    active->cleared = true;
    Serial.print("[ErrorHistory] Cleared active error: ");
    Serial.println(errorCodeToString(active->code));
  }
}

void ErrorHistory::getAll(ErrorRecord* output, int& count) {
  // Return all non-NONE errors, newest first
  count = 0;
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    int idx = (writeIndex + BUFFER_SIZE - 1 - i) % BUFFER_SIZE;
    if (buffer[idx].code != ERROR_NONE) {
      output[count++] = buffer[idx];
    }
  }
}
