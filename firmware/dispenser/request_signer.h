// firmware/dispenser/request_signer.h
//
// Request signing and the nonce pool (issue #8).
//
// WHAT THIS REPLACES.  Until #8 every request carried `X-API-Key`, the shared
// secret itself, over plain HTTP.  Anyone within range of the WLAN read it
// once and could dispense at will, and a captured request replayed with a
// fresh tx_id dispensed again.  `X-API-Key` is GONE — not deprecated, not
// behind a build flag: no device is deployed, so there is nothing to
// transition.  A request carrying it is unsigned, and unsigned is 401.
//
// HTTPS was evaluated and rejected for this board (see the issue): a BearSSL
// handshake costs 1-3 s and ~20 KB of the ESP8266's ~40 KB heap, and nothing
// in the payload is confidential.  What needs protecting is AUTHENTICITY and
// FRESHNESS, and neither needs encryption.
//
// Deliberately Arduino-free, like PulseFilter (#5) and WifiSupervisor (#7):
// it is handed the time and the entropy instead of calling millis() and the
// hardware RNG itself, so the whole decision runs in the native test build.
// The two SDK calls it implies stay in http_server.cpp.

#ifndef REQUEST_SIGNER_H
#define REQUEST_SIGNER_H

#include <stddef.h>
#include <stdint.h>
#include "hmac_sha256.h"

// A nonce is 128 bits, written as 32 lowercase hex characters.
#define NONCE_HEX_LEN 32
// A signature is the HMAC-SHA256 digest, 64 lowercase hex characters.
#define SIGNATURE_HEX_LEN 64

// How long an issued nonce stays usable.  Thirty seconds is the reservation
// TTL of the protocol: long enough that one `GET /nonce` covers a POST and the
// status polls that follow it, short enough that a nonce sniffed off the air
// is worthless by the time anybody has typed anything.
#define NONCE_TTL_MS 30000UL

// How many nonces may be outstanding at once.  Issuing a ninth evicts the
// oldest.  A terminal needs one; the slack is for a retry and a second
// terminal on the bench.  ACCEPTED CONSEQUENCE: somebody on the same WLAN can
// evict a terminal's nonce by asking for eight of his own, and the terminal
// then sees one 401 and retries with a fresh nonce.  He could equally flood
// the device with requests; a shared WLAN segment is not a threat this
// protects against, the dedicated VLAN in hardware/README.md is.
#define NONCE_POOL_SIZE 8

enum AuthResult {
  AUTH_OK = 0,
  // The signature is missing, malformed or wrong — or the request carried no
  // X-Nonce at all.  A client must NOT retry: nothing about a retry changes.
  AUTH_BAD_SIGNATURE = 1,
  // The nonce is unknown, expired, or already spent on a mutating request.
  // A client fetches a fresh nonce from GET /nonce and retries ONCE.  Because
  // every mutating request is idempotent by tx_id, that retry is safe.
  AUTH_BAD_NONCE = 2
};

// What a request is signed over, laid out byte for byte.  The whole protocol
// rests on both ends building this string identically, so it is stated here
// once and quoted verbatim in dispenser-protocol.md:
//
//     METHOD \n PATH \n BODY \n NONCE
//
// - METHOD is the HTTP method in upper case ("GET", "POST").
// - PATH is the request path with NO query string, always starting with "/".
// - BODY is the exact request body bytes; the empty string for a GET.
// - NONCE is the 32 hex characters exactly as GET /nonce issued them.
// - The separator is one LF (0x0A).  Three of them, none at the end.
//
// Method and path are in there so a signature for `GET /dispense/abc` cannot
// be lifted onto `POST /dispense`; the body is in there so a captured request
// cannot be re-aimed at another quantity; the nonce is in there so it cannot
// be replayed at all.
class RequestSigner {
public:
  // `key` must outlive the signer (it is the string literal from config.h).
  explicit RequestSigner(const char* key);

  // The signature a request MUST carry, as 64 lowercase hex characters plus a
  // NUL — so `out` is at least SIGNATURE_HEX_LEN + 1 bytes.  Public because
  // the bench tools and the tests sign with the same code the device verifies
  // with; a second implementation is a second protocol.
  static void sign(const char* key, const char* method, const char* path,
                   const char* body, size_t bodyLen, const char* nonce,
                   char* out);

  // Issues a nonce into the pool and writes NONCE_HEX_LEN + 1 bytes to `out`.
  // The four words are the caller's entropy — on the device the hardware RNG,
  // in a test a counter, which is exactly why the quality of that entropy is a
  // HARDWARE claim and not something any test here can make.
  void issueNonce(unsigned long now_ms, uint32_t w0, uint32_t w1, uint32_t w2,
                  uint32_t w3, char* out);

  // Verifies one request.  `mutating` is true for a request that changes
  // something (POST /dispense): it SPENDS the nonce, so the identical bytes
  // replayed afterwards are AUTH_BAD_NONCE.  A read-only request leaves the
  // nonce in the pool until it expires — a replayed GET returns the same
  // reading to the attacker who already saw it, which is nothing, and it is
  // what lets one nonce serve a whole transaction's status polls.
  AuthResult verify(unsigned long now_ms, const char* method, const char* path,
                    const char* body, size_t bodyLen, const char* nonce,
                    const char* signature, bool mutating);

  // Outstanding nonces that have not expired at `now_ms`; for the tests.
  int liveNonces(unsigned long now_ms) const;

private:
  struct Slot {
    char nonce[NONCE_HEX_LEN + 1];
    unsigned long issued_ms;
    bool used;   // occupied at all
    bool spent;  // already carried a mutating request
  };

  int findSlot(const char* nonce, unsigned long now_ms) const;
  static bool expired(unsigned long issued_ms, unsigned long now_ms);

  const char* signing_key;
  Slot pool[NONCE_POOL_SIZE];
  uint8_t next_slot;
};

#endif
