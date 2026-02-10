# ESP8266 Token Dispenser Firmware - Implementation Plan

> ⚠️ **OUTDATED DOCUMENT WARNING:**
> This design used transistor-based control circuits with specific pin assignments (D5/D6/D7/D8) and active-HIGH logic.
> **Current hardware uses PC817 optocouplers** with different pins (D1/D2/D5/D6) and **inverted logic** (active-LOW).
> See [hardware/README.md](../../hardware/README.md) for the current optocoupler-based design.

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build HTTP-controlled token dispenser firmware for Wemos D1 Mini (ESP8266) with crash recovery and jam detection.

**Architecture:** Component-based design with 6 modules: main sketch, HTTP server (ESPAsyncWebServer), transaction manager (state machine), hopper control (GPIO + interrupts), and flash persistence (EEPROM). API key authentication protects dispense operations while health endpoint remains open for monitoring.

**Tech Stack:** Arduino IDE, ESP8266 Arduino Core, ESPAsyncWebServer, ArduinoJson, EEPROM

---

## Prerequisites

**Before starting:**
1. Arduino IDE installed with ESP8266 board support
2. Libraries installed: ESPAsyncWebServer, ArduinoJson
3. Wemos D1 Mini connected via USB
4. Read: `docs/plans/2025-02-08-esp8266-firmware-design.md`
5. Read: `ARCHITECTURE.md` sections on API endpoints and recovery protocols

---

## Task 1: Project Setup & Configuration

**Files:**
- Create: `firmware/dispenser/dispenser.ino`
- Create: `firmware/dispenser/config.h`

**Step 1: Create config.h with all constants**

```cpp
// firmware/dispenser/config.h

#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define STATIC_IP IPAddress(192, 168, 4, 20)
#define GATEWAY IPAddress(192, 168, 4, 1)
#define SUBNET IPAddress(255, 255, 255, 0)

// API Authentication
#define API_KEY "change-this-secret-key-here"

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

#endif
```

**Step 2: Create minimal dispenser.ino**

```cpp
// firmware/dispenser/dispenser.ino

#include <ESP8266WiFi.h>
#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== Token Dispenser Starting ===");
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);

  // WiFi setup (next task will implement)
  Serial.println("Setup complete");
}

void loop() {
  // Main loop (will be populated)
  delay(100);
}
```

**Step 3: Test compilation**

Action:
- Open Arduino IDE
- File → Open → `firmware/dispenser/dispenser.ino`
- Tools → Board → "LOLIN(WEMOS) D1 R2 & mini"
- Sketch → Verify/Compile

Expected: "Done compiling" with no errors

**Step 4: Upload and verify serial output**

Action:
- Tools → Port → Select your port
- Sketch → Upload
- Tools → Serial Monitor → 115200 baud

Expected output:
```
=== Token Dispenser Starting ===
Firmware: 1.0.0
Setup complete
```

**Step 5: Commit**

```bash
git add firmware/dispenser/dispenser.ino firmware/dispenser/config.h
git commit -m "feat: add project structure and configuration

- Create config.h with WiFi, API key, GPIO pins, timing constants
- Create minimal dispenser.ino with serial output
- Verified compilation and upload to ESP8266"
```

---

## Task 2: WiFi Connection

**Files:**
- Modify: `firmware/dispenser/dispenser.ino`

**Step 1: Add WiFi connection code to setup()**

Replace setup() function:

```cpp
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== Token Dispenser Starting ===");
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.config(STATIC_IP, GATEWAY, SUBNET);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }

  Serial.println("Setup complete");
}
```

**Step 2: Update config.h with your WiFi credentials**

Edit `config.h` and set your actual WiFi SSID and password.

**Step 3: Upload and verify WiFi connection**

Action:
- Upload to ESP8266
- Open Serial Monitor (115200 baud)

Expected output:
```
=== Token Dispenser Starting ===
Firmware: 1.0.0
Connecting to WiFi: YourNetworkName
.....
WiFi connected!
IP address: 192.168.4.20
Setup complete
```

**Step 4: Verify static IP**

Action: `ping 192.168.4.20`

Expected: Successful ping response

**Step 5: Commit**

```bash
git add firmware/dispenser/dispenser.ino
git commit -m "feat: add WiFi connection with static IP

- Configure static IP address
- Wait for WiFi connection (max 15s)
- Print IP address to serial
- Tested successful connection"
```

---

## Task 3: Flash Storage Module

**Files:**
- Create: `firmware/dispenser/flash_storage.h`
- Create: `firmware/dispenser/flash_storage.cpp`

**Step 1: Create flash_storage.h interface**

```cpp
// firmware/dispenser/flash_storage.h

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>

// Transaction state enum
enum TransactionState {
  STATE_IDLE = 0,
  STATE_DISPENSING = 1,
  STATE_DONE = 2,
  STATE_ERROR = 3
};

// Persisted transaction structure
struct PersistedTransaction {
  char tx_id[17];           // "a3f8c012" + null terminator
  uint8_t quantity;         // 1-20 tokens
  uint8_t dispensed;        // Actual count
  TransactionState state;   // Current state
};

class FlashStorage {
public:
  void begin();
  bool hasPersistedTransaction();
  PersistedTransaction load();
  void persist(const PersistedTransaction& tx);
  void clear();
};

#endif
```

**Step 2: Create flash_storage.cpp implementation**

```cpp
// firmware/dispenser/flash_storage.cpp

#include "flash_storage.h"
#include <EEPROM.h>

#define EEPROM_SIZE 512
#define MAGIC_BYTE 0xAB  // Indicates valid data
#define ADDR_MAGIC 0
#define ADDR_DATA 1

void FlashStorage::begin() {
  EEPROM.begin(EEPROM_SIZE);
}

bool FlashStorage::hasPersistedTransaction() {
  return EEPROM.read(ADDR_MAGIC) == MAGIC_BYTE;
}

PersistedTransaction FlashStorage::load() {
  PersistedTransaction tx;

  if (!hasPersistedTransaction()) {
    // Return empty transaction
    memset(&tx, 0, sizeof(tx));
    tx.state = STATE_IDLE;
    return tx;
  }

  // Read from EEPROM
  EEPROM.get(ADDR_DATA, tx);
  return tx;
}

void FlashStorage::persist(const PersistedTransaction& tx) {
  // Write magic byte
  EEPROM.write(ADDR_MAGIC, MAGIC_BYTE);

  // Write transaction data
  EEPROM.put(ADDR_DATA, tx);

  // Commit to flash
  EEPROM.commit();
}

void FlashStorage::clear() {
  EEPROM.write(ADDR_MAGIC, 0x00);
  EEPROM.commit();
}
```

**Step 3: Test flash storage in dispenser.ino**

Add to top of dispenser.ino:
```cpp
#include "flash_storage.h"

FlashStorage flashStorage;
```

Add to setup() after WiFi:
```cpp
  // Initialize flash storage
  flashStorage.begin();

  if (flashStorage.hasPersistedTransaction()) {
    PersistedTransaction tx = flashStorage.load();
    Serial.println("Found persisted transaction:");
    Serial.print("  tx_id: ");
    Serial.println(tx.tx_id);
    Serial.print("  quantity: ");
    Serial.println(tx.quantity);
    Serial.print("  dispensed: ");
    Serial.println(tx.dispensed);
    Serial.print("  state: ");
    Serial.println(tx.state);
  } else {
    Serial.println("No persisted transaction");
  }
```

**Step 4: Upload and verify**

Expected output:
```
WiFi connected!
IP address: 192.168.4.20
No persisted transaction
Setup complete
```

**Step 5: Commit**

```bash
git add firmware/dispenser/flash_storage.h firmware/dispenser/flash_storage.cpp firmware/dispenser/dispenser.ino
git commit -m "feat: add flash storage module for crash recovery

- EEPROM-based persistence for transaction state
- Store/load: tx_id, quantity, dispensed, state
- Magic byte validation for data integrity
- Tested load on boot (no persisted data)"
```

---

## Task 4: Hopper Control Module

**Files:**
- Create: `firmware/dispenser/hopper_control.h`
- Create: `firmware/dispenser/hopper_control.cpp`

**Step 1: Create hopper_control.h interface**

```cpp
// firmware/dispenser/hopper_control.h

#ifndef HOPPER_CONTROL_H
#define HOPPER_CONTROL_H

#include <Arduino.h>
#include "config.h"

class HopperControl {
public:
  void begin();
  void startMotor();
  void stopMotor();
  uint8_t getPulseCount();
  void resetPulseCount();
  bool checkJam();
  bool isHopperLow();

private:
  static void IRAM_ATTR handleCoinPulse();
};

#endif
```

**Step 2: Create hopper_control.cpp with interrupt handling**

```cpp
// firmware/dispenser/hopper_control.cpp

#include "hopper_control.h"

// Static variables for ISR
static volatile uint8_t pulse_count = 0;
static volatile unsigned long last_pulse_time = 0;

void IRAM_ATTR HopperControl::handleCoinPulse() {
  pulse_count++;
  last_pulse_time = millis();
}

void HopperControl::begin() {
  // Configure GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);  // Motor off

  pinMode(COIN_PULSE_PIN, INPUT_PULLUP);
  pinMode(HOPPER_LOW_PIN, INPUT_PULLUP);

  // Attach interrupt for coin pulse (FALLING edge)
  attachInterrupt(digitalPinToInterrupt(COIN_PULSE_PIN),
                  handleCoinPulse, FALLING);

  // Initialize pulse tracking
  pulse_count = 0;
  last_pulse_time = millis();
}

void HopperControl::startMotor() {
  digitalWrite(MOTOR_PIN, HIGH);
  last_pulse_time = millis();  // Reset watchdog
}

void HopperControl::stopMotor() {
  digitalWrite(MOTOR_PIN, LOW);
}

uint8_t HopperControl::getPulseCount() {
  return pulse_count;
}

void HopperControl::resetPulseCount() {
  pulse_count = 0;
  last_pulse_time = millis();
}

bool HopperControl::checkJam() {
  // Check if no pulse received within JAM_TIMEOUT_MS
  if (millis() - last_pulse_time > JAM_TIMEOUT_MS) {
    return true;  // JAM detected
  }
  return false;
}

bool HopperControl::isHopperLow() {
  // Hopper low sensor is active LOW
  return digitalRead(HOPPER_LOW_PIN) == LOW;
}
```

**Step 3: Test hopper control in dispenser.ino**

Add to top:
```cpp
#include "hopper_control.h"

HopperControl hopperControl;
```

Add to setup() after flash storage:
```cpp
  // Initialize hopper control
  hopperControl.begin();
  Serial.println("Hopper control initialized");
  Serial.print("Hopper low: ");
  Serial.println(hopperControl.isHopperLow() ? "YES" : "NO");
```

**Step 4: Upload and test (no motor running yet)**

Expected output:
```
No persisted transaction
Hopper control initialized
Hopper low: NO
Setup complete
```

**Step 5: Commit**

```bash
git add firmware/dispenser/hopper_control.h firmware/dispenser/hopper_control.cpp firmware/dispenser/dispenser.ino
git commit -m "feat: add hopper control module with GPIO and interrupts

- Motor control (start/stop)
- Coin pulse interrupt handler (FALLING edge)
- Pulse counting with ISR
- Jam detection via 5s watchdog
- Hopper low sensor reading
- Tested GPIO initialization"
```

---

## Task 5: Dispense Manager Module

**Files:**
- Create: `firmware/dispenser/dispense_manager.h`
- Create: `firmware/dispenser/dispense_manager.cpp`

**Step 1: Create dispense_manager.h interface**

```cpp
// firmware/dispenser/dispense_manager.h

#ifndef DISPENSE_MANAGER_H
#define DISPENSE_MANAGER_H

#include <Arduino.h>
#include "flash_storage.h"
#include "hopper_control.h"

#define RING_BUFFER_SIZE 8

struct Transaction {
  char tx_id[17];
  uint8_t quantity;
  uint8_t dispensed;
  TransactionState state;
  unsigned long started_ms;
};

class DispenseManager {
public:
  DispenseManager(FlashStorage& storage, HopperControl& hopper);

  void begin();
  void loop();  // Called from main loop for watchdog

  // Transaction operations
  bool startDispense(const char* tx_id, uint8_t quantity);
  Transaction getTransaction(const char* tx_id);
  Transaction getActiveTransaction();
  bool isIdle();

  // Metrics
  uint16_t getTotalDispenses();
  uint16_t getSuccessful();
  uint16_t getJams();
  uint16_t getPartial();

private:
  FlashStorage& flashStorage;
  HopperControl& hopperControl;

  Transaction active_tx;

  // Ring buffer for idempotency (last 8 tx_ids)
  char tx_history[RING_BUFFER_SIZE][17];
  TransactionState history_states[RING_BUFFER_SIZE];
  uint8_t history_index;

  // Metrics
  uint16_t total_dispenses;
  uint16_t successful_count;
  uint16_t jam_count;
  uint16_t partial_count;

  bool findInHistory(const char* tx_id, Transaction& out_tx);
  void addToHistory(const char* tx_id, TransactionState state);
  void persistActiveTransaction();
};

#endif
```

**Step 2: Create dispense_manager.cpp implementation (part 1)**

```cpp
// firmware/dispenser/dispense_manager.cpp

#include "dispense_manager.h"
#include <string.h>

DispenseManager::DispenseManager(FlashStorage& storage, HopperControl& hopper)
  : flashStorage(storage), hopperControl(hopper) {
  memset(&active_tx, 0, sizeof(active_tx));
  active_tx.state = STATE_IDLE;

  memset(tx_history, 0, sizeof(tx_history));
  memset(history_states, 0, sizeof(history_states));
  history_index = 0;

  total_dispenses = 0;
  successful_count = 0;
  jam_count = 0;
  partial_count = 0;
}

void DispenseManager::begin() {
  // Load persisted transaction if exists
  if (flashStorage.hasPersistedTransaction()) {
    PersistedTransaction persisted = flashStorage.load();

    // Copy to active transaction
    strncpy(active_tx.tx_id, persisted.tx_id, 17);
    active_tx.quantity = persisted.quantity;
    active_tx.dispensed = persisted.dispensed;
    active_tx.state = persisted.state;

    // If crashed during dispensing, mark as error
    if (active_tx.state == STATE_DISPENSING) {
      active_tx.state = STATE_ERROR;
      persistActiveTransaction();

      Serial.println("Recovered from crash - partial dispense recorded");
    }

    // Add to history
    addToHistory(active_tx.tx_id, active_tx.state);
  }
}

bool DispenseManager::startDispense(const char* tx_id, uint8_t quantity) {
  // Check if already in history (idempotency)
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    // Return cached result
    active_tx = cached_tx;
    return true;  // Not an error, just idempotent
  }

  // Check if busy
  if (active_tx.state == STATE_DISPENSING) {
    return false;  // 409 Conflict
  }

  // Start new transaction
  strncpy(active_tx.tx_id, tx_id, 17);
  active_tx.quantity = quantity;
  active_tx.dispensed = 0;
  active_tx.state = STATE_DISPENSING;
  active_tx.started_ms = millis();

  // Persist to flash
  persistActiveTransaction();

  // Start motor
  hopperControl.resetPulseCount();
  hopperControl.startMotor();

  // Update metrics
  total_dispenses++;

  return true;
}
```

**Step 3: Create dispense_manager.cpp implementation (part 2)**

Continue in same file:

```cpp
void DispenseManager::loop() {
  if (active_tx.state != STATE_DISPENSING) {
    return;  // Nothing to monitor
  }

  // Update dispensed count from pulse counter
  active_tx.dispensed = hopperControl.getPulseCount();

  // Check for completion
  if (active_tx.dispensed >= active_tx.quantity) {
    hopperControl.stopMotor();
    active_tx.state = STATE_DONE;
    persistActiveTransaction();
    addToHistory(active_tx.tx_id, STATE_DONE);
    successful_count++;
    return;
  }

  // Check for jam
  if (hopperControl.checkJam()) {
    hopperControl.stopMotor();
    active_tx.state = STATE_ERROR;
    persistActiveTransaction();
    addToHistory(active_tx.tx_id, STATE_ERROR);
    jam_count++;

    if (active_tx.dispensed > 0) {
      partial_count++;
    }
    return;
  }
}

Transaction DispenseManager::getTransaction(const char* tx_id) {
  // Check active transaction
  if (strcmp(active_tx.tx_id, tx_id) == 0) {
    return active_tx;
  }

  // Check history
  Transaction cached_tx;
  if (findInHistory(tx_id, cached_tx)) {
    return cached_tx;
  }

  // Not found - return empty with IDLE state
  Transaction empty_tx;
  memset(&empty_tx, 0, sizeof(empty_tx));
  empty_tx.state = STATE_IDLE;
  return empty_tx;
}

Transaction DispenseManager::getActiveTransaction() {
  return active_tx;
}

bool DispenseManager::isIdle() {
  return active_tx.state != STATE_DISPENSING;
}

uint16_t DispenseManager::getTotalDispenses() { return total_dispenses; }
uint16_t DispenseManager::getSuccessful() { return successful_count; }
uint16_t DispenseManager::getJams() { return jam_count; }
uint16_t DispenseManager::getPartial() { return partial_count; }

// Private methods
bool DispenseManager::findInHistory(const char* tx_id, Transaction& out_tx) {
  for (int i = 0; i < RING_BUFFER_SIZE; i++) {
    if (strcmp(tx_history[i], tx_id) == 0) {
      // Found in history
      strncpy(out_tx.tx_id, tx_id, 17);
      out_tx.state = history_states[i];
      // Note: quantity/dispensed not stored in history (would need expansion)
      return true;
    }
  }
  return false;
}

void DispenseManager::addToHistory(const char* tx_id, TransactionState state) {
  strncpy(tx_history[history_index], tx_id, 17);
  history_states[history_index] = state;
  history_index = (history_index + 1) % RING_BUFFER_SIZE;
}

void DispenseManager::persistActiveTransaction() {
  PersistedTransaction persisted;
  strncpy(persisted.tx_id, active_tx.tx_id, 17);
  persisted.quantity = active_tx.quantity;
  persisted.dispensed = active_tx.dispensed;
  persisted.state = active_tx.state;

  flashStorage.persist(persisted);
}
```

**Step 4: Test dispense manager in dispenser.ino**

Add to top:
```cpp
#include "dispense_manager.h"

DispenseManager dispenseManager(flashStorage, hopperControl);
```

Add to setup():
```cpp
  // Initialize dispense manager
  dispenseManager.begin();
  Serial.println("Dispense manager initialized");
  Serial.print("State: ");
  Serial.println(dispenseManager.isIdle() ? "IDLE" : "BUSY");
```

Add to loop():
```cpp
void loop() {
  dispenseManager.loop();  // Monitor watchdog
  delay(10);  // Small delay
}
```

**Step 5: Upload and verify**

Expected output:
```
Hopper control initialized
Hopper low: NO
Dispense manager initialized
State: IDLE
Setup complete
```

**Step 6: Commit**

```bash
git add firmware/dispenser/dispense_manager.h firmware/dispenser/dispense_manager.cpp firmware/dispenser/dispenser.ino
git commit -m "feat: add dispense manager with state machine

- Transaction state machine (idle/dispensing/done/error)
- Idempotency via ring buffer (last 8 tx_ids)
- Watchdog monitoring in loop()
- Crash recovery from persisted state
- Metrics tracking (total, successful, jams, partial)
- Tested initialization and idle state"
```

---

## Task 6: HTTP Server Module

**Files:**
- Create: `firmware/dispenser/http_server.h`
- Create: `firmware/dispenser/http_server.cpp`

**Step 1: Create http_server.h interface**

```cpp
// firmware/dispenser/http_server.h

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <ESPAsyncWebServer.h>
#include "dispense_manager.h"
#include "hopper_control.h"
#include "config.h"

class HttpServer {
public:
  HttpServer(DispenseManager& manager, HopperControl& hopper);

  void begin();

private:
  DispenseManager& dispenseManager;
  HopperControl& hopperControl;
  AsyncWebServer server;

  // Endpoint handlers
  void handleHealth(AsyncWebServerRequest *request);
  void handleDispensePost(AsyncWebServerRequest *request, uint8_t *data,
                          size_t len, size_t index, size_t total);
  void handleDispenseGet(AsyncWebServerRequest *request);

  // Authentication
  bool checkAuth(AsyncWebServerRequest *request);

  // Utility
  const char* stateToString(TransactionState state);
};

#endif
```

**Step 2: Create http_server.cpp (part 1 - auth & utility)**

```cpp
// firmware/dispenser/http_server.cpp

#include "http_server.h"
#include <ArduinoJson.h>

HttpServer::HttpServer(DispenseManager& manager, HopperControl& hopper)
  : dispenseManager(manager), hopperControl(hopper), server(80) {
}

void HttpServer::begin() {
  // GET /health - NO AUTH
  server.on("/health", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleHealth(request);
  });

  // POST /dispense - REQUIRES AUTH
  server.on("/dispense", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // This is called after body is parsed
    },
    NULL,  // Upload handler
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
           size_t index, size_t total) {
      this->handleDispensePost(request, data, len, index, total);
    }
  );

  // GET /dispense/{tx_id} - REQUIRES AUTH
  server.on("/dispense/*", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleDispenseGet(request);
  });

  server.begin();
  Serial.println("HTTP server started on port 80");
}

bool HttpServer::checkAuth(AsyncWebServerRequest *request) {
  if (!request->hasHeader("X-API-Key")) {
    return false;
  }

  String apiKey = request->header("X-API-Key");
  return apiKey.equals(API_KEY);
}

const char* HttpServer::stateToString(TransactionState state) {
  switch (state) {
    case STATE_IDLE: return "idle";
    case STATE_DISPENSING: return "dispensing";
    case STATE_DONE: return "done";
    case STATE_ERROR: return "error";
    default: return "unknown";
  }
}
```

**Step 3: Create http_server.cpp (part 2 - health endpoint)**

Continue in same file:

```cpp
void HttpServer::handleHealth(AsyncWebServerRequest *request) {
  StaticJsonDocument<512> doc;

  doc["status"] = "ok";
  doc["uptime"] = millis() / 1000;
  doc["firmware"] = FIRMWARE_VERSION;

  Transaction active = dispenseManager.getActiveTransaction();
  doc["dispenser"] = stateToString(active.state);
  doc["hopper_low"] = hopperControl.isHopperLow();

  // Metrics
  JsonObject metrics = doc.createNestedObject("metrics");
  metrics["total_dispenses"] = dispenseManager.getTotalDispenses();
  metrics["successful"] = dispenseManager.getSuccessful();
  metrics["jams"] = dispenseManager.getJams();
  metrics["partial"] = dispenseManager.getPartial();

  uint16_t failures = dispenseManager.getTotalDispenses()
                    - dispenseManager.getSuccessful()
                    - dispenseManager.getJams();
  metrics["failures"] = failures;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}
```

**Step 4: Create http_server.cpp (part 3 - dispense endpoints)**

Continue in same file:

```cpp
void HttpServer::handleDispensePost(AsyncWebServerRequest *request,
                                    uint8_t *data, size_t len,
                                    size_t index, size_t total) {
  // Check authentication
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  // Parse JSON body
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  const char* tx_id = doc["tx_id"];
  uint8_t quantity = doc["quantity"];

  if (!tx_id || quantity == 0 || quantity > MAX_TOKENS) {
    request->send(400, "application/json",
                  "{\"error\":\"invalid tx_id or quantity\"}");
    return;
  }

  // Try to start dispense
  bool started = dispenseManager.startDispense(tx_id, quantity);

  if (!started && !dispenseManager.isIdle()) {
    // Busy - return 409
    Transaction active = dispenseManager.getActiveTransaction();

    StaticJsonDocument<256> response;
    response["error"] = "busy";
    response["active_tx_id"] = active.tx_id;
    response["active_state"] = stateToString(active.state);

    String responseStr;
    serializeJson(response, responseStr);
    request->send(409, "application/json", responseStr);
    return;
  }

  // Return current transaction state
  Transaction tx = dispenseManager.getTransaction(tx_id);

  StaticJsonDocument<256> response;
  response["tx_id"] = tx.tx_id;
  response["state"] = stateToString(tx.state);
  response["quantity"] = tx.quantity;
  response["dispensed"] = tx.dispensed;

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}

void HttpServer::handleDispenseGet(AsyncWebServerRequest *request) {
  // Check authentication
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  // Extract tx_id from URL: /dispense/abc123
  String url = request->url();
  String tx_id = url.substring(10);  // After "/dispense/"

  if (tx_id.length() == 0) {
    request->send(400, "application/json", "{\"error\":\"missing tx_id\"}");
    return;
  }

  Transaction tx = dispenseManager.getTransaction(tx_id.c_str());

  if (tx.state == STATE_IDLE && tx.tx_id[0] == '\0') {
    // Not found
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  StaticJsonDocument<256> response;
  response["tx_id"] = tx.tx_id;
  response["state"] = stateToString(tx.state);
  response["quantity"] = tx.quantity;
  response["dispensed"] = tx.dispensed;

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}
```

**Step 5: Add HTTP server to dispenser.ino**

Add to top:
```cpp
#include "http_server.h"

HttpServer httpServer(dispenseManager, hopperControl);
```

Add to setup() after dispenseManager.begin():
```cpp
  // Start HTTP server
  httpServer.begin();
```

**Step 6: Upload and test health endpoint**

Upload firmware, then test:

```bash
curl http://192.168.4.20/health
```

Expected response:
```json
{
  "status": "ok",
  "uptime": 42,
  "firmware": "1.0.0",
  "dispenser": "idle",
  "hopper_low": false,
  "metrics": {
    "total_dispenses": 0,
    "successful": 0,
    "jams": 0,
    "partial": 0,
    "failures": 0
  }
}
```

**Step 7: Test authentication**

Without API key:
```bash
curl -X POST http://192.168.4.20/dispense \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":1}'
```

Expected: `{"error":"unauthorized"}`

With API key (update with your key from config.h):
```bash
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":3}'
```

Expected: `{"tx_id":"test123","state":"dispensing","quantity":3,"dispensed":0}`

**Step 8: Test status query**

```bash
curl -H "X-API-Key: change-this-secret-key-here" \
  http://192.168.4.20/dispense/test123
```

Expected: Transaction status (state will be "done" or "error" depending on if motor actually ran)

**Step 9: Commit**

```bash
git add firmware/dispenser/http_server.h firmware/dispenser/http_server.cpp firmware/dispenser/dispenser.ino
git commit -m "feat: add HTTP server with all endpoints

- ESPAsyncWebServer on port 80
- GET /health (no auth) - health status and metrics
- POST /dispense (auth required) - start transaction
- GET /dispense/{tx_id} (auth required) - query status
- API key authentication via X-API-Key header
- JSON request/response handling with ArduinoJson
- Tested all endpoints via curl"
```

---

## Task 7: Integration Testing

**No new files - testing only**

**Step 1: Test full dispense flow (simulated)**

Since we don't have hopper hardware yet, test the state machine:

```bash
# Start dispense
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"integration001","quantity":3}'
```

Expected: `{"state":"dispensing",...}`

**Step 2: Query status multiple times**

```bash
# Query status (repeat a few times)
curl -H "X-API-Key: change-this-secret-key-here" \
  http://192.168.4.20/dispense/integration001
```

Watch state transition: `dispensing` → `error` (after 5s jam timeout, since no pulses)

**Step 3: Test idempotency**

Send same tx_id again:

```bash
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"integration001","quantity":3}'
```

Expected: Returns cached result (state "error" from history)

**Step 4: Test conflict handling**

Start new transaction while one is active:

```bash
# Start first transaction
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"tx001","quantity":2}'

# Immediately try second transaction
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"tx002","quantity":1}'
```

Expected second response: `409 Conflict` with active transaction info

**Step 5: Test crash recovery**

```bash
# Start a transaction
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: change-this-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"crash_test","quantity":5}'

# Power cycle the ESP8266 (unplug USB, wait 2s, plug back in)

# After reboot, check serial monitor
```

Expected serial output:
```
Found persisted transaction:
  tx_id: crash_test
  ...
Recovered from crash - partial dispense recorded
```

Query the transaction:
```bash
curl -H "X-API-Key: change-this-secret-key-here" \
  http://192.168.4.20/dispense/crash_test
```

Expected: State should be "error" (recovered from crash)

**Step 6: Verify metrics**

```bash
curl http://192.168.4.20/health
```

Check that metrics reflect all tests:
- `total_dispenses` > 0
- `jams` > 0 (from 5s timeouts)
- `successful` = 0 (no real hardware connected)

**Step 7: Document test results**

Create test results file:

```bash
echo "# Integration Test Results

Date: $(date)

## Tests Passed
- ✅ Health endpoint (no auth)
- ✅ Authentication enforcement
- ✅ Dispense start
- ✅ Status query
- ✅ Idempotency (cached results)
- ✅ Conflict handling (409 busy)
- ✅ Crash recovery (persisted state)
- ✅ Jam detection (5s timeout)
- ✅ Metrics tracking

## Tests Pending Hardware
- Coin pulse interrupt (needs real hopper)
- Successful completion (needs tokens)
- Partial dispense (needs jam simulation)

## Next Steps
- Connect Azkoyen Hopper hardware
- Test with real token dispensing
- Verify pulse counting accuracy
" > firmware/TESTING.md
```

**Step 8: Commit**

```bash
git add firmware/TESTING.md
git commit -m "test: integration testing without hardware

- Tested full API surface (health, dispense, query)
- Verified authentication enforcement
- Confirmed idempotency via ring buffer
- Tested conflict handling (409 busy)
- Verified crash recovery from EEPROM
- Validated jam detection timeout
- Documented test results and pending hardware tests"
```

---

## Task 8: Final Documentation & Cleanup

**Files:**
- Modify: `firmware/README.md`
- Create: `firmware/dispenser/CHANGELOG.md`

**Step 1: Update README with actual setup steps**

Verify `firmware/README.md` has correct:
- Library installation instructions
- Board selection steps
- config.h configuration guide
- Testing commands

Already complete from brainstorming phase - no changes needed.

**Step 2: Create CHANGELOG**

```markdown
# Changelog

All notable changes to the ESP8266 Token Dispenser firmware.

## [1.0.0] - 2025-02-08

### Added
- Initial release
- HTTP API server (ESPAsyncWebServer)
- GET /health endpoint with metrics
- POST /dispense endpoint with API key auth
- GET /dispense/{tx_id} status query
- Transaction state machine (idle/dispensing/done/error)
- Flash persistence for crash recovery (EEPROM)
- Coin pulse interrupt handling
- 5-second jam detection watchdog
- Idempotency via ring buffer (last 8 transactions)
- Metrics tracking (total, successful, jams, partial)
- WiFi connection with static IP
- Hopper low sensor support

### Tested
- All HTTP endpoints
- Authentication enforcement
- Idempotent transaction handling
- Conflict detection (409 busy)
- Crash recovery from persisted state
- Jam timeout detection

### Pending
- Hardware integration testing with Azkoyen Hopper U-II
- Pulse counting accuracy validation
- Real-world dispense verification
```

**Step 3: Add .gitignore**

```bash
cat > firmware/.gitignore << 'EOF'
# Arduino build artifacts
*.hex
*.elf
*.bin
*.map

# Local configuration (don't commit secrets)
dispenser/config.local.h

# IDE
.vscode/
*.code-workspace
EOF
```

**Step 4: Final code review checklist**

Create checklist file:

```markdown
# Code Review Checklist

## Before Hardware Testing

- [ ] Changed API_KEY in config.h
- [ ] Set correct WiFi credentials
- [ ] Verified static IP configuration
- [ ] All endpoints tested via curl
- [ ] Crash recovery tested (power cycle)
- [ ] Idempotency verified
- [ ] Conflict handling (409) confirmed
- [ ] Metrics tracking validated
- [ ] Serial monitor output clean (no errors)

## Hardware Integration

- [ ] D5 → Level shifter → Motor control (12V)
- [ ] D6 → Hopper opto-sensor (pulse output)
- [ ] D8 → Hopper low sensor (optional)
- [ ] Common ground ESP8266 ↔ Hopper
- [ ] Separate 12V/2A power for hopper
- [ ] Hopper jumpers: STANDARD + PULSES mode

## Safety

- [ ] Motor stops on jam timeout (5s)
- [ ] Motor stops on reboot (GPIO default LOW)
- [ ] Flash writes on every state change
- [ ] No blocking delays in loop()
- [ ] ISR functions marked IRAM_ATTR

## Production

- [ ] Firmware version updated
- [ ] API key is strong secret
- [ ] WiFi password is strong
- [ ] Static IP documented
- [ ] Backup of config.h saved securely
```

**Step 5: Commit final docs**

```bash
git add firmware/dispenser/CHANGELOG.md firmware/.gitignore
git commit -m "docs: add changelog and gitignore

- CHANGELOG.md documenting v1.0.0 features
- .gitignore for build artifacts and secrets
- Code review checklist for hardware integration
- All documentation complete and ready for deployment"
```

---

## Summary

**Implementation Complete! 🎉**

**What was built:**
- ✅ Complete ESP8266 firmware (6 modules)
- ✅ HTTP API server with authentication
- ✅ Transaction state machine
- ✅ Crash recovery via flash persistence
- ✅ Jam detection watchdog
- ✅ Metrics tracking
- ✅ Integration tested (simulated)

**Ready for:**
1. Hardware integration with Azkoyen Hopper U-II
2. Real-world dispense testing
3. Production deployment

**Next Steps:**
1. Connect hopper hardware (see wiring checklist)
2. Test pulse counting with real tokens
3. Verify jam detection with physical jam
4. Deploy to production with strong API key

**Files Created:**
- `firmware/dispenser/dispenser.ino` (main)
- `firmware/dispenser/config.h`
- `firmware/dispenser/flash_storage.h/cpp`
- `firmware/dispenser/hopper_control.h/cpp`
- `firmware/dispenser/dispense_manager.h/cpp`
- `firmware/dispenser/http_server.h/cpp`
- `firmware/README.md`
- `firmware/TESTING.md`
- `firmware/.gitignore`
- `firmware/dispenser/CHANGELOG.md`

**Total commits:** 8 commits (one per major task)
