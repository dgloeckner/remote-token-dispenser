#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

// firmware/dispenser/http_server.cpp

#include "http_server.h"
#include "log.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <stdlib.h>

HttpServer::HttpServer(DispenseManager& manager, HopperControl& hopper)
  : dispenseManager(manager), hopperControl(hopper), server(80) {
}

void HttpServer::begin() {
  // GET /health - NO AUTH
  server.on("/health", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleHealth(request);
  });

  // POST /dispense - REQUIRES AUTH
  //
  // The body callback only collects bytes; the request handler answers.  That
  // order is the fix for two bugs at once (issue #4): a POST with no body never
  // reaches a body callback, so an empty lambda there left the caller waiting
  // for its own timeout — and a body split across TCP segments reached the old
  // handler as a fragment, which parsed as "400 invalid json" at random.
  server.on("/dispense", HTTP_POST,
    [this](AsyncWebServerRequest *request) {
      this->handleDispensePost(request);
    },
    NULL,  // Upload handler
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len,
           size_t index, size_t total) {
      this->collectDispenseBody(request, data, len, index, total);
    }
  );

  // GET /dispense/{tx_id} - REQUIRES AUTH
  server.on("/dispense/*", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleDispenseGet(request);
  });

  // GET /debug - REQUIRES AUTH.  Raw pin levels for the bench (issue #6).
  server.on("/debug", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleDebug(request);
  });

  // Nothing else is registered, and POST /reset in particular is not: a fault
  // is ended by a power cycle and by nothing else (owner decision,
  // 2026-09-20).  The async server answers 404 for it, which is the protocol.

  server.begin();
  LOG_INFO("HTTP server started on port 80");
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

  doc["protocol"] = PROTOCOL_VERSION;
  // ONE state and ONE fault (issue #6).  The old document carried `status`
  // (ok | degraded | error) next to `dispenser` (idle | dispensing | error),
  // the terminal ORed the two together, and nothing decided which of them won
  // when they disagreed.  `status` was hard-coded "ok" on top of that.
  doc["state"] = deviceStateToString(dispenseManager.getDeviceState());
  doc["fault"] = faultToString(dispenseManager.getFault());
  doc["fault_code"] = dispenseManager.getFaultCode();
  doc["uptime"] = millis() / 1000;
  doc["firmware"] = FIRMWARE_VERSION;

  // WiFi information
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["rssi"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();
  wifi["ssid"] = WiFi.SSID();

  // No `gpio` block: raw pin levels are GET /debug now, and there is no
  // hopper_low anywhere any more — the empty sensor is a factory option this
  // hopper does not have, so the line reported "not empty" forever.

  // Metrics
  JsonObject metrics = doc.createNestedObject("metrics");

  // Transaction-level metrics
  metrics["total_dispenses"] = dispenseManager.getTotalDispenses();
  metrics["successful"] = dispenseManager.getSuccessful();
  metrics["jams"] = dispenseManager.getJams();
  metrics["partial"] = dispenseManager.getPartial();
  metrics["crashes"] = dispenseManager.getCrashes();

  uint16_t failures = dispenseManager.getTotalDispenses()
                    - dispenseManager.getSuccessful()
                    - dispenseManager.getJams();
  metrics["failures"] = failures;

  // Token-level metrics
  metrics["requested_tokens"] = dispenseManager.getRequestedTokens();
  metrics["dispensed_tokens"] = dispenseManager.getDispensedTokens();
  // Tokens past the requested quantity: the ones that fall while the disc
  // coasts (issue #5).  A single transaction shows its own overrun in
  // `dispensed`; this is the number anybody actually watches.
  metrics["overrun_tokens"] = dispenseManager.getOverrunTokens();
  // Falling edges the pulse filter rejected as noise.  A hopper whose sensor
  // bounces is visible here before it is visible in a short dispense.
  metrics["filtered_pulses"] = hopperControl.getFilteredPulseCount();

  // The decoded hopper errors, newest first.  There is no separate `error`
  // block any more: what an active error MEANS is the fault above, and the
  // records have no `cleared` flag because nothing clears them short of a
  // power cycle.
  JsonArray history = doc.createNestedArray("error_history");
  ErrorRecord records[5];
  int count;
  hopperControl.errorHistory.getAll(records, count);

  for (int i = 0; i < count; i++) {
    JsonObject e = history.createNestedObject();
    e["code"] = (int)records[i].code;
    e["type"] = errorCodeToString(records[i].code);
    e["timestamp"] = records[i].timestamp;
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void HttpServer::handleDebug(AsyncWebServerRequest *request) {
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  JsonDocument doc;
  JsonObject gpio = doc.createNestedObject("gpio");

  JsonObject coinPulse = gpio.createNestedObject("coin_pulse");
  coinPulse["raw"] = hopperControl.getCoinPulseRaw();
  coinPulse["active"] = hopperControl.isCoinPulseActive();

  JsonObject errorSignal = gpio.createNestedObject("error_signal");
  errorSignal["raw"] = hopperControl.getErrorSignalRaw();
  errorSignal["active"] = hopperControl.isErrorSignalActive();

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void HttpServer::sendTransaction(AsyncWebServerRequest *request, const Transaction& tx) {
  JsonDocument response;
  response["tx_id"] = tx.tx_id;
  response["state"] = stateToString(tx.state);
  response["quantity"] = tx.quantity;
  response["dispensed"] = tx.dispensed;
  // Required on every transaction response: false means the count is a lower
  // bound because the device lost power mid-dispense (dispenser-protocol.md).
  response["count_reliable"] = tx.count_reliable;
  // Also required, also on every response (issue #6): WHY it failed, if it
  // did.  One flat "error" made a jam, an empty hopper and a dead sensor the
  // same row on the terminal.
  response["error_code"] = tx.error_code;
  response["error_type"] = txErrorTypeToString(tx.error_kind, tx.error_code);

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}

void HttpServer::collectDispenseBody(AsyncWebServerRequest *request,
                                     uint8_t *data, size_t len,
                                     size_t index, size_t total) {
  // The body buffer hangs off the request, because several requests can be in
  // flight at once and a single member would mix their bytes.  _tempObject is
  // the slot the async server provides for exactly this; it frees it with the
  // request, so it must come from malloc and not from new.
  if (request->_tempObject == NULL) {
    request->_tempObject = malloc(sizeof(RequestBody));
    if (request->_tempObject == NULL) {
      return;  // out of heap: the handler answers 413 on the empty buffer
    }
    ((RequestBody*)request->_tempObject)->reset();
  }
  ((RequestBody*)request->_tempObject)->append(data, len, index, total);
}

void HttpServer::handleDispensePost(AsyncWebServerRequest *request) {
  // Everything below runs in the async TCP callback, so it must stay cheap:
  // parse, decide, answer.  The flash commit and the motor start happen in
  // loop(), where DispenseManager picks the request slot up (issue #4).
  if (!checkAuth(request)) {
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return;
  }

  if (!request->hasHeader("Content-Type") ||
      request->header("Content-Type").indexOf("application/json") == -1) {
    request->send(415, "application/json",
                  "{\"error\":\"content-type must be application/json\"}");
    return;
  }

  RequestBody* body = (RequestBody*)request->_tempObject;

  // No body callback ever ran: the request carried no body at all.  This is
  // the case that used to hang.
  if (body == NULL || body->isEmpty()) {
    request->send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }

  if (body->status() == BODY_TOO_LARGE) {
    request->send(413, "application/json", "{\"error\":\"body too large\"}");
    return;
  }

  if (body->status() != BODY_COMPLETE) {
    // A stream with a gap, or one that stopped short of its announced length.
    request->send(400, "application/json", "{\"error\":\"incomplete body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body->data(), body->size());
  if (error) {
    LOG_DEBUG("POST /dispense: JSON parse failed: %s", error.c_str());
    request->send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }

  if (!doc.containsKey("tx_id") || !doc["tx_id"].is<const char*>() ||
      !doc.containsKey("quantity") || !doc["quantity"].is<uint8_t>()) {
    request->send(400, "application/json", "{\"error\":\"invalid request format\"}");
    return;
  }

  const char* tx_id = doc["tx_id"];
  uint8_t quantity = doc["quantity"];

  size_t tx_id_len = strlen(tx_id);
  if (tx_id_len == 0 || tx_id_len > 16 || quantity == 0 || quantity > MAX_TOKENS) {
    request->send(400, "application/json",
                  "{\"error\":\"invalid tx_id or quantity\"}");
    return;
  }

  DispenseOutcome outcome = dispenseManager.requestDispense(tx_id, quantity);

  if (outcome == DISPENSE_TX_ID_REUSED) {
    // The caller contradicted itself: a tx_id it already used, with another
    // quantity.  Answering with the old quantity would look like a successful
    // retry of a request that was never made.
    request->send(409, "application/json", "{\"error\":\"tx_id reused\"}");
    return;
  }

  if (outcome == DISPENSE_FAULT) {
    // The device needs a human.  It says which kind, because "clear the jam"
    // and "the hopper reports a motor fault" are different errands — and it
    // never says how to clear it from here, because there is no way: the
    // instruction is to pull the plug (owner decision, 2026-09-20).
    JsonDocument response;
    response["error"] = "fault";
    response["fault"] = faultToString(dispenseManager.getFault());
    response["fault_code"] = dispenseManager.getFaultCode();

    String responseStr;
    serializeJson(response, responseStr);
    request->send(409, "application/json", responseStr);
    return;
  }

  if (outcome == DISPENSE_BUSY) {
    // Busy always means ANOTHER transaction now — a retry of the running one
    // is answered above with its current state (issue #2).
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
  sendTransaction(request, dispenseManager.getTransaction(tx_id));
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

  sendTransaction(request, tx);
}
