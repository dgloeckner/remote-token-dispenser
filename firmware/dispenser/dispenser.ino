// firmware/dispenser/dispenser.ino

#include <ESP8266WiFi.h>
#include "config.h"
#include "log.h"
#include "flash_storage.h"
#include "rtc_count_memory.h"
#include "hopper_control.h"
#include "dispense_manager.h"
#include "http_server.h"

FlashStorage flashStorage;
HopperControl hopperControl;
RtcCountMemory rtcCountMemory;
DispenseManager dispenseManager(flashStorage, hopperControl, rtcCountMemory);
HttpServer httpServer(dispenseManager, hopperControl);

void setup() {
  // LOG_BAUD, not 9600: platformio.ini's monitor_speed has always said 115200,
  // and at 9600 a line of output is a stall long enough to matter (issue #4).
  Serial.begin(LOG_BAUD);
  delay(1000);

  LOG_INFO("=== Token Dispenser %s starting ===", FIRMWARE_VERSION);
  LOG_INFO("connecting to WiFi: %s", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.config(STATIC_IP, GATEWAY, SUBNET);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_INFO("WiFi connected, IP %s", WiFi.localIP().toString().c_str());
  } else {
    LOG_ERROR("WiFi connection failed after %d attempts", attempts);
  }

  flashStorage.begin();

  PersistedRecord persistedRecord;
  if (flashStorage.load(persistedRecord)) {
    const PersistedTransaction& tx = persistedRecord.active;
    LOG_DEBUG("persisted record: tx_id=%s quantity=%u dispensed=%u state=%d ring=%u",
              tx.tx_id, (unsigned)tx.quantity, (unsigned)tx.dispensed, (int)tx.state,
              (unsigned)persistedRecord.ring_index);
  } else {
    LOG_DEBUG("no persisted record");
  }

  hopperControl.begin();
  LOG_INFO("hopper low: %s", hopperControl.isHopperLow() ? "YES" : "NO");

  dispenseManager.begin();

  httpServer.begin();

  LOG_INFO("setup complete, state %s", dispenseManager.isIdle() ? "IDLE" : "BUSY");
}

void loop() {
  // Two jobs: start whatever the HTTP layer accepted into the request slot,
  // and watch the running transaction.  Everything with a side effect — the
  // flash commit, the motor — happens from here and never from the async TCP
  // callback, so the delay below is also the worst-case start latency (#4).
  dispenseManager.loop();

  // Update error decoder (check timeouts, process new errors)
  hopperControl.updateErrorDecoder();

  // 10ms delay is safe: coin pulses (30ms) are counted via hardware interrupt
  // (asynchronous, not blocked by delay), and tokens arrive ~2.5s apart.
  // This delay just prevents spinning at 100% CPU while waiting for events.
  delay(10);
}
