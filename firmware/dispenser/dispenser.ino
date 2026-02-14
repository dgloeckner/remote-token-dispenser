// firmware/dispenser/dispenser.ino

#include <ESP8266WiFi.h>
#include "config.h"
#include "flash_storage.h"
#include "hopper_control.h"
#include "dispense_manager.h"
#include "http_server.h"

FlashStorage flashStorage;
HopperControl hopperControl;
DispenseManager dispenseManager(flashStorage, hopperControl);
HttpServer httpServer(dispenseManager, hopperControl);

void setup() {
  Serial.begin(9600);  // Lower baud rate for reliable debug output
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
    Serial.flush();  // Ensure output is sent
    delay(100);
    Serial.println(">>> DEBUG: Continuing setup...");
    Serial.flush();
  } else {
    Serial.println("\nWiFi connection failed!");
  }

  Serial.println(">>> DEBUG: Initializing flash storage...");
  Serial.flush();
  // Initialize flash storage
  flashStorage.begin();
  Serial.println(">>> DEBUG: Flash storage initialized");

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

  // Initialize hopper control
  Serial.println(">>> DEBUG: About to call hopperControl.begin()...");
  Serial.flush();
  hopperControl.begin();
  Serial.println(">>> DEBUG: hopperControl.begin() completed");
  Serial.println("Hopper control initialized");
  Serial.print("Hopper low: ");
  Serial.println(hopperControl.isHopperLow() ? "YES" : "NO");

  // Initialize dispense manager
  dispenseManager.begin();
  Serial.println("Dispense manager initialized");
  Serial.print("State: ");
  Serial.println(dispenseManager.isIdle() ? "IDLE" : "BUSY");

  // Start HTTP server
  httpServer.begin();

  Serial.println("Setup complete");
}

void loop() {
  dispenseManager.loop();  // Monitor watchdog and completion

  // Update error decoder (check timeouts, process new errors)
  hopperControl.updateErrorDecoder();

  // 10ms delay is safe: coin pulses (30ms) are counted via hardware interrupt
  // (asynchronous, not blocked by delay), and tokens arrive ~2.5s apart.
  // This delay just prevents spinning at 100% CPU while waiting for events.
  delay(10);
}
