// firmware/dispenser/error_history.h

#ifndef ERROR_HISTORY_H
#define ERROR_HISTORY_H

#include <Arduino.h>
#include "error_decoder.h"

// Single error record in ring buffer.
//
// There is no `cleared` flag any more (issue #6): a decoded hopper error
// raises a device fault, and a fault is ended by a reboot and by nothing
// else — so the flag was false on every record that mattered and the
// "self-healing" it described never had a case left to heal.
struct ErrorRecord {
  ErrorCode code;
  unsigned long timestamp;  // millis() when detected
};

// Ring buffer for last 5 errors
class ErrorHistory {
private:
  static const uint8_t BUFFER_SIZE = 5;
  ErrorRecord buffer[BUFFER_SIZE];
  uint8_t writeIndex;

public:
  ErrorHistory();
  void addError(ErrorCode code);
  void getAll(ErrorRecord* output, int& count);  // output must have space for BUFFER_SIZE entries
};

#endif
