// firmware/dispenser/config.h

#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration - CHANGE THESE IN config.local.h (see config.local.h.example)
#ifndef WIFI_SSID
  #define WIFI_SSID "YourNetworkName"
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "YourPassword"
#endif
#ifndef STATIC_IP
  #define STATIC_IP IPAddress(192, 168, 4, 20)
#endif
#ifndef GATEWAY
  #define GATEWAY IPAddress(192, 168, 4, 1)
#endif
#ifndef SUBNET
  #define SUBNET IPAddress(255, 255, 255, 0)
#endif

// Protocol version handshake (dispenser-protocol.md).
// There are no devices in the field: protocol 2 replaces protocol 1 outright,
// and a client refuses any other version instead of adapting to it.
#define PROTOCOL_VERSION 2

// API Authentication - CHANGE THIS IN config.local.h
#ifndef API_KEY
  #define API_KEY "change-this-secret-key-here"
#endif

// GPIO Pins (Wemos D1 Mini ESP8266)
//
// ⚠️ CRITICAL HARDWARE REQUIREMENTS:
//   1. Hopper DIP switch MUST be set to NEGATIVE mode (active LOW control)
//   2. PC817 optocoupler #1 (motor control) R1 MUST be modified: add 330Ω in parallel with stock 1kΩ
//
// OPTOCOUPLER WIRING (PC817 modules with D1→IN+, GND→IN-):
//   - Motor control (D1): GPIO HIGH → LED ON → OUT LOW (~0.1V) → motor ON (NEGATIVE mode)
//                         GPIO LOW  → LED OFF → OUT HIGH (~6V) → motor OFF
//   - Input signals: LOW = signal active (coin pulse/error/empty detected)
//
// WHY THESE VALUES:
//   - R1 modification (1kΩ || 330Ω = 248Ω): Provides 13.3mA for PC817 saturation
//     Without modification: Only 3.3mA → phototransistor won't saturate → unreliable control
//   - R2 (10kΩ pull-up): Creates voltage divider with hopper input (~10kΩ) → HIGH = ~6V
//     This is acceptable for NEGATIVE mode (threshold ~3-4V)
//   - OUT voltage ranges: LED ON < 0.5V (reliable LOW), LED OFF ~6V (reliable HIGH for NEGATIVE mode)
#define MOTOR_PIN          D1    // GPIO5  - Motor control output (via PC817 #1)
#define COIN_PULSE_PIN     D7    // GPIO13 - Coin pulse input (via PC817 #2)
#define ERROR_SIGNAL_PIN   D5    // GPIO14 - Hopper error input (via PC817 #3)
#define HOPPER_LOW_PIN     D6    // GPIO12 - Empty sensor input (via PC817 #4)

// Timing Constants
#define JAM_TIMEOUT_MS     5000   // 5 seconds per token
#define MAX_TOKENS         20     // Max tokens per transaction

// Hopper Mode (configured via DIP switches inside hopper)
// ⚠️ REQUIRED: Set to NEGATIVE mode for active LOW control
// POSITIVE mode will cause inverted motor behavior (motor runs at wrong times)
#define HOPPER_MODE_NEGATIVE  // Document the required mode (not a code constant)

// Hardware Specs (Azkoyen Hopper U-II PULSES mode)
#define PULSE_DURATION_MS  30     // Expected pulse duration

// Pulse filtering and the settling window (issue #5).
//
// COIN_PULSE_MIN_GAP_MS — the smallest spacing between two edges that can both
// be tokens.  The datasheet pins the numbers on either side of it: one coin is
// a single LOW phase of 30-65 ms (PULSE_DURATION_MS above,
// docs/azkoyen-hopper-protocol.md section 3.4), and the hopper dispenses
// roughly one coin per second.  So two real tokens are never closer than the
// pulse itself, and 20 ms sits below the shortest legal pulse with margin
// while being a hundred times more than a bouncing optocoupler edge or an EMI
// spike from the motor switching on the same supply needs.  Anything closer
// than this to the last ACCEPTED edge is noise, and noise used to be a token:
// each spurious edge shortened the dispense by one coin.
//
// DISPENSE_SETTLING_MS — how long a transaction keeps counting after the motor
// has been told to stop.  The disc coasts, and a token already past the wheel
// still falls; the ISR stop (commit a9f15af) cut the motor sooner but could
// never make the last token unfall.  500 ms is four times the simulator's
// coast delay (COAST_DELAY_MS = 120 ms in firmware/hopper-simulator/) and well
// inside the 5 s jam timeout, so the window can never be mistaken for a jam.
// A token that arrives in it is counted and reported, which is why `dispensed`
// may exceed `quantity` (dispenser-protocol.md).
#define COIN_PULSE_MIN_GAP_MS  20
#define DISPENSE_SETTLING_MS  500

// Set from the build (-DFIRMWARE_VERSION='"…"' in platformio.ini) so a release
// cannot go out carrying a debug string the way 1.1.0-DEBUG-error-decoding did.
// The fallback keeps a plain checkout of the sketch compiling in the Arduino
// IDE, which passes no flags.
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "1.2.0"
#endif

// Include local configuration (not tracked in git)
// Copy config.local.h.example to config.local.h and customize
#if __has_include("config.local.h")
  #include "config.local.h"
#endif

#endif
