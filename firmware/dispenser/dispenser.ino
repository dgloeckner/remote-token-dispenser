// firmware/dispenser/dispenser.ino

#include <ESP8266WiFi.h>
#include "config.h"
#include "log.h"
#include "flash_storage.h"
#include "rtc_count_memory.h"
#include "hopper_control.h"
#include "dispense_manager.h"
#include "http_server.h"
#include "wifi_supervisor.h"

FlashStorage flashStorage;
HopperControl hopperControl;
RtcCountMemory rtcCountMemory;
DispenseManager dispenseManager(flashStorage, hopperControl, rtcCountMemory);
WifiSupervisor wifiSupervisor;
HttpServer httpServer(dispenseManager, hopperControl, wifiSupervisor);

void setup() {
  // LOG_BAUD, not 9600: platformio.ini's monitor_speed has always said 115200,
  // and at 9600 a line of output is a stall long enough to matter (issue #4).
  Serial.begin(LOG_BAUD);
  delay(1000);

  LOG_INFO("=== Token Dispenser %s starting ===", FIRMWARE_VERSION);
  LOG_INFO("connecting to WiFi: %s", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  // Three SDK defaults this device cannot live with (issue #7):
  //
  // - Modem sleep is the default for WIFI_STA.  On an ESP8266 that answers
  //   HTTP it is the usual cause of a request that takes seconds or times out
  //   once and succeeds on retry — the pattern the terminal's retry logic was
  //   written around.  The dispenser is mains-powered; there is nothing to
  //   save here.
  // - `persistent(false)`: the SDK otherwise writes the credentials to flash
  //   on every begin(), which is a flash write per boot for data that comes
  //   from config.local.h anyway.
  // - `setAutoReconnect(true)` is stated rather than assumed: it is the first
  //   line of defence, and the supervisor in loop() is the second.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
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
    // Not fatal any more, and not final either: the HTTP server still starts
    // (a device on a flaky AP that joins a second later should serve), but the
    // supervisor below now restarts the board if the link is still down a
    // minute from here.  Before #7 this line was the end of the story and the
    // only way back was a walk to the boathouse.
    LOG_ERROR("WiFi connection failed after %d attempts; the supervisor will "
              "restart in %lus unless it joins", attempts, WIFI_RESTART_AFTER_MS / 1000);
  }

  wifiSupervisor.begin(WiFi.status() == WL_CONNECTED, millis());

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

  // The connectivity supervisor (issue #7).  The rule itself is unit-tested
  // in WifiSupervisor; what is left here is the two SDK calls it needs.  It
  // runs AFTER the dispense logic and never fires while the motor is on, so a
  // restart can only ever happen between transactions.
  //
  // A restart clears the device fault, exactly as any boot does — that is the
  // whole of owner decision 3 and not a hole to patch: a jam that is still
  // there faults the next dispense again, having dispensed and billed nothing.
  if (wifiSupervisor.update(WiFi.status() == WL_CONNECTED,
                            dispenseManager.getDeviceState() == DEVICE_DISPENSING,
                            millis())) {
    LOG_ERROR("WiFi down for %lus with nothing dispensing: restarting",
              wifiSupervisor.disconnectedFor(millis()) / 1000);
    Serial.flush();
    ESP.restart();
  }

  // 10ms delay is safe: coin pulses (30ms) are counted via hardware interrupt
  // (asynchronous, not blocked by delay), and tokens arrive ~2.5s apart.
  // This delay just prevents spinning at 100% CPU while waiting for events.
  delay(10);
}
