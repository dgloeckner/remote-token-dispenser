// firmware/dispenser/error_history.h

#ifndef ERROR_HISTORY_H
#define ERROR_HISTORY_H

#include <Arduino.h>
#include "error_decoder.h"

// Single error record in ring buffer
struct ErrorRecord {
  ErrorCode code;
  unsigned long timestamp;  // millis() when detected
  bool cleared;             // false = active, true = cleared by successful dispense
};

// Ring buffer for last 5 errors
class ErrorHistory {
private:
  ErrorRecord buffer[5];
  uint8_t writeIndex;

public:
  ErrorHistory();
  void addError(ErrorCode code);
  ErrorRecord* getActive();  // Returns first non-cleared error (newest first), or nullptr
  void clearActive();        // Marks active error as cleared
  void getAll(ErrorRecord* output, int& count);  // Returns all non-NONE errors (newest first)
};

#endif
