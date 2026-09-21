// firmware/dispenser/http_server.h

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <ESPAsyncWebServer.h>
#include "dispense_manager.h"
#include "hopper_control.h"
#include "request_body.h"
#include "wifi_supervisor.h"
#include "config.h"

class HttpServer {
public:
  HttpServer(DispenseManager& manager, HopperControl& hopper, WifiSupervisor& wifi);

  void begin();

private:
  DispenseManager& dispenseManager;
  HopperControl& hopperControl;
  // Only for `wifi.reconnects` in the health document (issue #7): the count
  // of times the link came back, which nothing else on the device remembers.
  WifiSupervisor& wifiSupervisor;
  AsyncWebServer server;

  // Endpoint handlers
  void handleHealth(AsyncWebServerRequest *request);
  // The raw pin levels.  They left /health in issue #6: a monitor cannot act
  // on them and a member cannot read them, and the one reader that wants them
  // (the TUI on the bench) can send a key.
  void handleDebug(AsyncWebServerRequest *request);
  // The POST is two callbacks, on purpose (issue #4).  collectDispenseBody()
  // only copies bytes; handleDispensePost() runs once the body is in, and is
  // reached even when there is no body at all — which is how an empty POST
  // gets its 400 instead of hanging until the caller times out.
  void collectDispenseBody(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total);
  void handleDispensePost(AsyncWebServerRequest *request);
  void handleDispenseGet(AsyncWebServerRequest *request);

  // Authentication
  bool checkAuth(AsyncWebServerRequest *request);

  // Utility
  const char* stateToString(TransactionState state);
  // The transaction body every 200 carries, including the required
  // count_reliable / error_code / error_type (dispenser-protocol.md).
  void sendTransaction(AsyncWebServerRequest *request, const Transaction& tx);
};

#endif
