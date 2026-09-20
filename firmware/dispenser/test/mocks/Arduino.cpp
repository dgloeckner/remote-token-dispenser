// Mock Arduino implementation
#include "Arduino.h"

MockSerial Serial;
unsigned long _mock_millis = 0;
unsigned long _mock_micros = 0;
