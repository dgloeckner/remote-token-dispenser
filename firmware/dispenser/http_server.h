// firmware/dispenser/http_server.h

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <ESPAsyncWebServer.h>
#include "dispense_manager.h"
#include "hopper_control.h"
#include "request_body.h"
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
};

#endif
