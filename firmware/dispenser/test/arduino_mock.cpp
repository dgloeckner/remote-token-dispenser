// The ONE translation unit that defines the Arduino mock's symbols.
//
// mocks/Arduino.h only declares `Serial`, `_mock_millis` and `_mock_micros`
// `extern` — a header that defines them would give every translation unit its
// own copy and the native link would fail on duplicate symbols.  This file
// must stay in the test directory *root*: PlatformIO compiles the sources of
// the test suite itself, not the ones in subdirectories like mocks/, so a
// definition parked next to the header is never built and the link ends in
// "undefined reference to `Serial'".
#include "mocks/Arduino.h"

MockSerial Serial;
unsigned long _mock_millis = 0;
unsigned long _mock_micros = 0;
