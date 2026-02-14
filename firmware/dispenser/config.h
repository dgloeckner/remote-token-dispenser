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
#define FIRMWARE_VERSION   "1.1.0-DEBUG-error-decoding"

// Include local configuration (not tracked in git)
// Copy config.local.h.example to config.local.h and customize
#if __has_include("config.local.h")
  #include "config.local.h"
#endif

#endif
