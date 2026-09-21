// firmware/dispenser/log.h
//
// One line per state transition, and nothing else unless someone asks for it.
//
// Why this exists (issue #4): the firmware printed roughly 500 bytes per POST
// at 9600 baud.  The UART FIFO holds 128 bytes; everything past it is sent at
// ~960 bytes/s with the CPU waiting on it.  That is half a second of blocking
// inside code that has a motor to watch — and it used to happen inside the
// async TCP callback, with the caller waiting too.
//
// Two things fix it, and both are here:
//   - the port runs at LOG_BAUD (115200, the rate platformio.ini's
//     monitor_speed has always assumed), so the same text costs a twelfth;
//   - LOG_DEBUG is compiled out of a release build, so the per-token and
//     per-field chatter is not sent at all.
//
// Rules:
//   - A pin write comes BEFORE the log line that announces it.  The motor must
//     stop when it is told to, not when the UART has drained.
//   - LOG_INFO is for state transitions: one line, past tense, no field dumps.
//   - Anything inside a loop over tokens or JSON fields is LOG_DEBUG.

#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

// Overridable from platformio.ini with -DLOG_LEVEL=…
#ifndef LOG_LEVEL
  #define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG_BAUD 115200

#if LOG_LEVEL >= LOG_LEVEL_ERROR
  #define LOG_ERROR(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_ERROR(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
  #define LOG_INFO(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_INFO(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_DEBUG(fmt, ...) do {} while (0)
#endif

#endif
