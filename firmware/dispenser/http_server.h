// firmware/dispenser/http_server.h

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <ESPAsyncWebServer.h>
#include "dispense_manager.h"
#include "hopper_control.h"
#include "request_body.h"
#include "request_signer.h"
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
  // The nonce pool and the HMAC (issue #8).  Everything it needs beyond the
  // key is handed in by the caller: millis() and the hardware RNG stay here,
  // the decision stays testable.
  RequestSigner signer;

  // Endpoint handlers
  // GET /nonce — unauthenticated, and it has to be: a client cannot sign
  // anything until it holds one, and a random number is not a secret.
  void handleNonce(AsyncWebServerRequest *request);
  // /health answers TWO documents off the same URL (issue #8).  Unsigned it
  // is three fields — is it there, can it sell, does it need a human — and
  // `"authenticated": false` says so out loud, so a client that forgot to
  // sign sees a reduced document rather than guessing at old firmware.
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

  // Authentication.  Verifies X-Nonce / X-Signature over the request and, on
  // failure, sends the 401 itself — including the `reason` the client acts
  // on: "nonce" means fetch a fresh one and retry once, "signature" means
  // stop.  `mutating` spends the nonce (POST), so the identical bytes replayed
  // are refused.  There is no X-API-Key path here or anywhere else.
  bool requireSignature(AsyncWebServerRequest *request, bool mutating,
                        const char *body, size_t bodyLen);
  // The request path with any query string cut off — what the signature is
  // computed over on both ends.
  static String signedPath(AsyncWebServerRequest *request);

  // Utility
  const char* stateToString(TransactionState state);
  // The transaction body every 200 carries, including the required
  // count_reliable / error_code / error_type (dispenser-protocol.md).
  void sendTransaction(AsyncWebServerRequest *request, const Transaction& tx);
};

#endif
