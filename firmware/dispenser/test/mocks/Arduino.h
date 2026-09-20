// Mock Arduino.h for native testing
//
// Only what the Arduino-free production sources listed in the native
// build_src_filter actually use.  If a new production file needs more of the
// Arduino API than this, that is a sign it belongs behind an interface
// (see ../../interfaces.h) rather than in the native build.
#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <stdint.h>
#include <string.h>

// ESP-only attribute, meaningless on the host
#define IRAM_ATTR

// Mock Serial — swallows everything, so tests stay quiet
class MockSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void print(const char* str) { (void)str; }
    void print(int val) { (void)val; }
    void print(unsigned int val) { (void)val; }
    void print(unsigned long val) { (void)val; }
    void println(const char* str) { (void)str; }
    void println(int val) { (void)val; }
    void println(unsigned int val) { (void)val; }
    void println(unsigned long val) { (void)val; }
    void println() {}
};

extern MockSerial Serial;

// Mock time functions.  Tests set _mock_millis directly.
extern unsigned long _mock_millis;
extern unsigned long _mock_micros;
inline unsigned long millis() { return _mock_millis; }
inline unsigned long micros() { return _mock_micros; }
inline void setMockMillis(unsigned long ms) { _mock_millis = ms; }
inline void setMockMicros(unsigned long us) { _mock_micros = us; }

// Constants
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Interrupt guards — no-ops on the host
inline void noInterrupts() {}
inline void interrupts() {}

// Mock GPIO functions
inline void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
inline void digitalWrite(uint8_t pin, uint8_t val) { (void)pin; (void)val; }
inline int digitalRead(uint8_t pin) { (void)pin; return LOW; }

#endif // ARDUINO_H_MOCK
