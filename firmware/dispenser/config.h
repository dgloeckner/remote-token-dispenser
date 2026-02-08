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
#define MOTOR_PIN          D5    // GPIO14 - Motor control output
#define COIN_PULSE_PIN     D6    // GPIO12 - Interrupt input (30ms pulses)
#define ERROR_SIGNAL_PIN   D7    // GPIO13 - Hopper error (optional)
#define HOPPER_LOW_PIN     D8    // GPIO15 - Low token warning (optional)

// Timing Constants
#define JAM_TIMEOUT_MS     5000   // 5 seconds per token
#define MAX_TOKENS         20     // Max tokens per transaction

// Hardware Specs (Azkoyen Hopper U-II PULSES mode)
#define PULSE_DURATION_MS  30     // Expected pulse duration
#define FIRMWARE_VERSION   "1.0.0"

// Include local configuration (not tracked in git)
// Copy config.local.h.example to config.local.h and customize
#if __has_include("config.local.h")
  #include "config.local.h"
#endif

#endif
