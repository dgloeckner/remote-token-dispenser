// Mock Arduino.h for native testing
#ifndef ARDUINO_H_MOCK
#define ARDUINO_H_MOCK

#include <stdint.h>
#include <string.h>

// Mock Serial
class MockSerial {
public:
    void begin(unsigned long baud) {}
    void print(const char* str) {}
    void print(int val) {}
    void print(unsigned long val) {}
    void println(const char* str) {}
    void println(int val) {}
    void println(unsigned long val) {}
    void println() {}
};

extern MockSerial Serial;

// Mock time functions
extern unsigned long _mock_millis;
inline unsigned long millis() { return _mock_millis; }
inline void setMockMillis(unsigned long ms) { _mock_millis = ms; }

// Constants
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// Mock GPIO functions
inline void pinMode(uint8_t pin, uint8_t mode) {}
inline void digitalWrite(uint8_t pin, uint8_t val) {}
inline int digitalRead(uint8_t pin) { return LOW; }

#endif // ARDUINO_H_MOCK
