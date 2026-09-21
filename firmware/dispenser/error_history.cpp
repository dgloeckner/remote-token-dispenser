// firmware/dispenser/error_history.cpp

#include "log.h"
#include "error_history.h"

ErrorHistory::ErrorHistory() : writeIndex(0) {
  // Initialize buffer with ERROR_NONE
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    buffer[i] = {ERROR_NONE, 0};
  }
}

void ErrorHistory::addError(ErrorCode code) {
  buffer[writeIndex] = {code, millis()};
  writeIndex = (writeIndex + 1) % BUFFER_SIZE;

  LOG_ERROR("hopper error %s recorded at %lu ms", errorCodeToString(code), millis());
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
