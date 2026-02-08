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
