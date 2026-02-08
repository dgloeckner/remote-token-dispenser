# ESP8266 Firmware Design - Token Dispenser

**Date:** 2025-02-08
**Hardware:** Wemos D1 Mini (ESP8266)
**Purpose:** HTTP-controlled token dispenser with crash recovery and jam detection

---

## Overview

ESP8266 firmware that implements an HTTP API for controlling an Azkoyen Hopper U-II coin/token dispenser. Provides idempotent transaction handling, crash recovery via flash persistence, and real-time jam detection.

---

## Hardware Platform

**Board:** LOLIN(WEMOS) D1 R2 & mini (ESP8266-based)

**Key Specs:**
- Single core, 80MHz
- 80KB RAM
- WiFi 802.11 b/g/n
- Arduino IDE compatible with D-pin labels (D0-D8)

---

## Architecture

### Component-Based Organization

```
esp32/
├── dispenser/
│   ├── dispenser.ino          # Main sketch (setup/loop)
│   ├── config.h               # WiFi, pins, API key, constants
│   ├── http_server.cpp        # HTTP endpoints & JSON handling
│   ├── http_server.h
│   ├── dispense_manager.cpp   # Transaction state machine
│   ├── dispense_manager.h
│   ├── hopper_control.cpp     # Motor + sensor GPIO + interrupts
│   ├── hopper_control.h
│   ├── flash_storage.cpp      # EEPROM/LittleFS persistence
│   └── flash_storage.h
└── README.md                  # Setup & library requirements
```

### Component Responsibilities

**dispenser.ino** (Main)
- Initialize WiFi, HTTP server, GPIO
- Load persisted state on boot
- Main loop: watchdog monitoring, state transitions
- Minimal logic - delegates to modules

**http_server** (API Layer)
- ESPAsyncWebServer on port 80
- Endpoints: `GET /health`, `POST /dispense`, `GET /dispense/{tx_id}`
- JSON parsing (ArduinoJson)
- API key authentication (X-API-Key header)
- Calls dispense_manager for business logic

**dispense_manager** (Business Logic)
- Transaction state machine: idle → dispensing → done/error
- Idempotency via tx_id ring buffer (last 8 transactions)
- Conflict detection (single resource lock)
- Coordinates hopper_control and flash_storage
- Tracks metrics: total_dispenses, successful, jams, etc.

**hopper_control** (Hardware Interface)
- Motor GPIO control (start/stop)
- Coin pulse interrupt handler (FALLING edge)
- Watchdog timer (5s jam detection via millis())
- Optional: error signal, hopper low detection

**flash_storage** (Persistence)
- EEPROM or LittleFS
- Persist: `{tx_id, quantity, dispensed, state}` on changes
- Load state on boot for crash recovery
- Simple key-value interface

---

## GPIO Pin Assignments

**Wemos D1 Mini (ESP8266) - Using D-pin Labels:**

```cpp
// config.h

#define MOTOR_PIN          D5    // GPIO14 - Motor control output
#define COIN_PULSE_PIN     D6    // GPIO12 - Interrupt input (30ms pulses)
#define ERROR_SIGNAL_PIN   D7    // GPIO13 - Hopper error (optional)
#define HOPPER_LOW_PIN     D8    // GPIO15 - Low token warning (optional)
```

**Pin Mapping:**
- D5 = GPIO14
- D6 = GPIO12
- D7 = GPIO13
- D8 = GPIO15

**Rationale:**
- All pins safe for general use
- D6 interrupt-capable for pulse counting
- Avoided D0/D3/D4 (boot conflicts)

**Hardware Interface:**
- Motor: 3.3V GPIO → Level shifter/relay → 12V Azkoyen motor
- Sensors: 3.3V compatible, internal pull-up resistors
- Azkoyen: Separate 12V/2A power supply

---

## Configuration Management

**config.h - Central Configuration File:**

```cpp
// WiFi Configuration
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define STATIC_IP "192.168.4.20"

// API Authentication
#define API_KEY "change-this-secret-key"  // CHANGE BEFORE DEPLOYMENT

// GPIO Pins
#define MOTOR_PIN          D5
#define COIN_PULSE_PIN     D6
#define ERROR_SIGNAL_PIN   D7
#define HOPPER_LOW_PIN     D8

// Timing Constants
#define JAM_TIMEOUT_MS     5000   // 5 seconds per token
#define MAX_TOKENS         20     // Max tokens per transaction

// Hardware Specs (Azkoyen Hopper U-II PULSES mode)
#define PULSE_DURATION_MS  30     // Expected pulse duration
#define TOKENS_PER_SECOND  0.4    // ~2.5 seconds per token
```

**Security Notes:**
- Change API_KEY before deployment
- Keep WiFi credentials out of version control (use .gitignore)
- Consider config.local.h for sensitive values

---

## API Authentication

**Dispense Operations:** Require API key

```http
POST /dispense HTTP/1.1
Host: 192.168.4.20
X-API-Key: your-secret-key-here
Content-Type: application/json
```

**Health Endpoint:** No authentication required (monitoring access)

```http
GET /health HTTP/1.1
Host: 192.168.4.20
```

**Unauthorized Response (401):**
```json
{
  "error": "unauthorized"
}
```

---

## Transaction State Machine

**States:**
```
idle → dispensing → done
  ↓         ↓
  └────→ error
```

**State Definitions:**
- **idle**: Ready for new transaction
- **dispensing**: Motor running, counting tokens
- **done**: Completed successfully (dispensed == quantity)
- **error**: Jam/timeout/hardware fault

**Transaction Structure:**
```cpp
struct Transaction {
    char tx_id[17];           // "a3f8c012" (client-generated)
    uint8_t quantity;         // 1-20 tokens requested
    uint8_t dispensed;        // Actual count from sensor
    State state;              // Current state
    unsigned long started_ms; // millis() when started
    unsigned long last_pulse_ms; // Last token timestamp
};
```

**State Transitions:**

1. **idle → dispensing:**
   - Validate tx_id (check idempotency)
   - Check conflict (only one transaction at a time)
   - Start motor, set watchdog timer
   - Persist to flash

2. **dispensing → done:**
   - `dispensed >= quantity`
   - Stop motor
   - Persist final state

3. **dispensing → error:**
   - Jam detected (5s timeout)
   - Hardware error signal
   - Stop motor, persist partial count

---

## Watchdog & Error Handling

### Jam Detection

**Mechanism:** millis()-based watchdog timer

```cpp
// hopper_control.cpp

#define JAM_TIMEOUT_MS 5000

volatile uint8_t pulse_count = 0;
unsigned long last_pulse_time = 0;

void IRAM_ATTR onCoinPulse() {
    pulse_count++;
    last_pulse_time = millis();  // Reset watchdog
}

bool checkJam() {
    if (millis() - last_pulse_time > JAM_TIMEOUT_MS) {
        return true;  // No token in 5 seconds = JAM
    }
    return false;
}
```

**Main Loop Monitoring:**

```cpp
// dispenser.ino loop()

void loop() {
    if (active_tx.state == DISPENSING) {
        // Check jam
        if (hopper_control.checkJam()) {
            stopMotor();
            active_tx.state = ERROR;
            active_tx.dispensed = pulse_count;
            flash_storage.persist(active_tx);  // Save partial count
        }

        // Check completion
        if (pulse_count >= active_tx.quantity) {
            stopMotor();
            active_tx.state = DONE;
            active_tx.dispensed = pulse_count;
            flash_storage.persist(active_tx);
        }
    }
}
```

### Crash Recovery

**On Boot:**

```cpp
void setup() {
    // ... WiFi, GPIO init ...

    if (flash_storage.hasPersistedTransaction()) {
        active_tx = flash_storage.load();

        // Crashed during dispense?
        if (active_tx.state == DISPENSING) {
            active_tx.state = ERROR;  // Mark as error
            flash_storage.persist(active_tx);
            // Client will poll GET /dispense/{tx_id} and reconcile
        }
    }
}
```

**Guarantees:**
- Exact `dispensed` count preserved across power loss
- Client can query tx_id to get actual outcome
- No double-dispensing on retry

---

## HTTP Endpoints Implementation

### Libraries

- **ESPAsyncWebServer:** Non-blocking HTTP server
- **ArduinoJson:** JSON parsing/serialization

### Endpoint: GET /health

**No authentication required**

**Response:**
```json
{
  "status": "ok",
  "uptime": 84230,
  "firmware": "1.0.0",
  "dispenser": "idle",
  "hopper_low": false,
  "metrics": {
    "total_dispenses": 1247,
    "successful": 1189,
    "jams": 3,
    "partial": 2,
    "failures": 53,
    "last_error": "2025-02-07T14:23:00Z",
    "last_error_type": "jam"
  }
}
```

### Endpoint: POST /dispense

**Requires X-API-Key header**

**Request:**
```json
{
  "tx_id": "a3f8c012",
  "quantity": 3
}
```

**Response (200 OK - Started):**
```json
{
  "tx_id": "a3f8c012",
  "state": "dispensing",
  "quantity": 3,
  "dispensed": 0
}
```

**Response (409 Conflict - Busy):**
```json
{
  "error": "busy",
  "active_tx_id": "previous_tx",
  "active_state": "dispensing"
}
```

**Response (200 OK - Idempotent):**
```json
{
  "tx_id": "a3f8c012",
  "state": "done",
  "quantity": 3,
  "dispensed": 3
}
```

### Endpoint: GET /dispense/{tx_id}

**Requires X-API-Key header**

**Response:**
```json
{
  "tx_id": "a3f8c012",
  "state": "done",
  "quantity": 3,
  "dispensed": 3
}
```

---

## Dependencies

### Arduino Libraries

**Required:**

1. **ESP8266WiFi** (built-in with ESP8266 core)
2. **ESPAsyncWebServer** (by me-no-dev)
   - Install via Library Manager
   - Auto-installs dependency: ESPAsyncTCP
3. **ArduinoJson** (by Benoit Blanchon)
   - Version 6.x recommended
   - Install via Library Manager
4. **EEPROM** (built-in) or **LittleFS** (built-in)

### Board Support

**ESP8266 Arduino Core:**

Add to Arduino IDE → Preferences → Additional Board Manager URLs:
```
http://arduino.esp8266.com/stable/package_esp8266com_index.json
```

Then: Tools → Board Manager → Install "esp8266"

### Board Selection

Tools → Board → **"LOLIN(WEMOS) D1 R2 & mini"**

---

## Installation Steps

1. **Install Arduino IDE** (if not already installed)

2. **Add ESP8266 Board Support:**
   - Preferences → Additional Board Manager URLs
   - Add: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools → Board Manager → Search "esp8266" → Install

3. **Install Libraries:**
   - Tools → Manage Libraries
   - Search and install:
     - "ESPAsyncWebServer" by me-no-dev
     - "ArduinoJson" by Benoit Blanchon

4. **Select Board:**
   - Tools → Board → ESP8266 Boards → "LOLIN(WEMOS) D1 R2 & mini"

5. **Configure:**
   - Edit `config.h` with your WiFi credentials and API key
   - Set static IP address

6. **Upload:**
   - Connect Wemos D1 Mini via USB
   - Tools → Port → Select appropriate port
   - Sketch → Upload

---

## Testing Approach

### Unit Testing (Manual)

1. **Health Endpoint:**
   ```bash
   curl http://192.168.4.20/health
   ```

2. **Dispense (Unauthorized):**
   ```bash
   curl -X POST http://192.168.4.20/dispense \
     -H "Content-Type: application/json" \
     -d '{"tx_id":"test123","quantity":1}'
   # Should return 401
   ```

3. **Dispense (Authorized):**
   ```bash
   curl -X POST http://192.168.4.20/dispense \
     -H "X-API-Key: your-secret-key-here" \
     -H "Content-Type: application/json" \
     -d '{"tx_id":"test123","quantity":3}'
   ```

4. **Status Query:**
   ```bash
   curl -H "X-API-Key: your-secret-key-here" \
     http://192.168.4.20/dispense/test123
   ```

### Integration Testing

1. Test jam detection (manually block sensor)
2. Test crash recovery (power cycle during dispense)
3. Test idempotency (send same tx_id twice)
4. Test conflict handling (concurrent requests)

---

## Deployment

### Production Checklist

- [ ] Change API_KEY in config.h
- [ ] Set correct WIFI_SSID and WIFI_PASSWORD
- [ ] Configure STATIC_IP for your network
- [ ] Test all endpoints
- [ ] Verify GPIO pins match hardware wiring
- [ ] Test jam detection
- [ ] Test crash recovery
- [ ] Document API key for client configuration

### Wiring Verification

Before powering on:
- Motor control (D5) → Level shifter → 12V relay/MOSFET → Azkoyen motor
- Coin pulse (D6) → Azkoyen opto-sensor output (with pull-up)
- Azkoyen 12V power supply separate from ESP8266 3.3V logic
- Common ground between ESP8266 and Azkoyen

---

## Future Enhancements

1. **OTA Updates:** Over-the-air firmware updates
2. **WiFi Manager:** Captive portal for WiFi configuration
3. **HTTPS:** TLS encryption for API (requires ESP32 for better performance)
4. **mDNS:** Access via `dispenser.local` instead of IP
5. **Web Dashboard:** Simple web UI for status monitoring

---

## References

- [ARCHITECTURE.md](../../ARCHITECTURE.md) - System architecture
- [dispenser-protocol.md](../../dispenser-protocol.md) - HTTP protocol spec
- [ESPAsyncWebServer Documentation](https://github.com/me-no-dev/ESPAsyncWebServer)
- [ArduinoJson Documentation](https://arduinojson.org/)
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
