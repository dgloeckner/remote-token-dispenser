#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

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

void HttpServer::handleHealth(AsyncWebServerRequest *request) {
  JsonDocument doc;

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

void HttpServer::handleDispensePost(AsyncWebServerRequest *request,
                                    uint8_t *data, size_t len,
                                    size_t index, size_t total) {
  // Check authentication
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  // Validate Content-Type
  if (!request->hasHeader("Content-Type") ||
      request->header("Content-Type").indexOf("application/json") == -1) {
    request->send(415, "application/json", "{\"error\":\"content-type must be application/json\"}");
    return;
  }

  // Parse JSON body
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  // Add type validation
  if (!doc.containsKey("tx_id") || !doc["tx_id"].is<const char*>() ||
      !doc.containsKey("quantity") || !doc["quantity"].is<uint8_t>()) {
    request->send(400, "application/json", "{\"error\":\"invalid request format\"}");
    return;
  }

  const char* tx_id = doc["tx_id"];
  uint8_t quantity = doc["quantity"];

  // Add tx_id length validation
  size_t tx_id_len = strlen(tx_id);
  if (tx_id_len == 0 || tx_id_len > 16 || quantity == 0 || quantity > MAX_TOKENS) {
    request->send(400, "application/json",
                  "{\"error\":\"invalid tx_id or quantity\"}");
    return;
  }

  // Try to start dispense
  bool started = dispenseManager.startDispense(tx_id, quantity);

  if (!started && !dispenseManager.isIdle()) {
    // Busy - return 409
    Transaction active = dispenseManager.getActiveTransaction();

    JsonDocument response;
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

  JsonDocument response;
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
  if (!url.startsWith("/dispense/")) {
    request->send(400, "application/json", "{\"error\":\"invalid url\"}");
    return;
  }
  String tx_id = url.substring(10);  // After "/dispense/"
  int queryStart = tx_id.indexOf('?');
  if (queryStart != -1) {
    tx_id = tx_id.substring(0, queryStart);
  }
  tx_id.trim();

  if (tx_id.length() == 0 || tx_id.length() > 16) {
    request->send(400, "application/json", "{\"error\":\"invalid tx_id\"}");
    return;
  }

  Transaction tx = dispenseManager.getTransaction(tx_id.c_str());

  if (tx.state == STATE_IDLE && tx.tx_id[0] == '\0') {
    // Not found
    request->send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  JsonDocument response;
  response["tx_id"] = tx.tx_id;
  response["state"] = stateToString(tx.state);
  response["quantity"] = tx.quantity;
  response["dispensed"] = tx.dispensed;

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}
