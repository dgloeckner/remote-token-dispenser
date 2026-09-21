// firmware/dispenser/hmac_sha256.h
//
// SHA-256 and HMAC-SHA256, from scratch and Arduino-free (issue #8).
//
// WHY NOT BearSSL, which the ESP8266 core already ships: the required test
// `signature_over_known_vector_matches` runs in the NATIVE build, on the host,
// where there is no BearSSL — and the test README's first rule is that a test
// may never exercise a second copy of the production logic.  A device that
// signs with BearSSL and a test that checks a hand-written twin would prove
// nothing about the firmware.  One implementation, compiled into both builds,
// checked against the RFC 4231 vectors, is the only arrangement in which a
// green test means the device signs correctly.
//
// It is also not a new dependency: `platformio.ini` is fully pinned since #7,
// and the cheapest way to keep it pinned is to add nothing to lib_deps.
//
// ~300 bytes of RAM and roughly 2 KB of flash; one HMAC over a dispense
// request is two SHA-256 blocks plus the message, microseconds on an ESP8266.

#ifndef HMAC_SHA256_H
#define HMAC_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

class Sha256 {
public:
  Sha256() { reset(); }
  void reset();
  void update(const uint8_t* data, size_t len);
  // Writes SHA256_DIGEST_SIZE bytes.  The context is unusable afterwards
  // until reset().
  void finish(uint8_t out[SHA256_DIGEST_SIZE]);

private:
  void compress(const uint8_t block[SHA256_BLOCK_SIZE]);

  uint32_t state[8];
  uint64_t bitlen;
  uint8_t buffer[SHA256_BLOCK_SIZE];
  size_t buffered;
};

// HMAC-SHA256 as RFC 2104 defines it: a key longer than the block is hashed
// first, a shorter one is zero-padded.
void hmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* msg, size_t msgLen,
                uint8_t out[SHA256_DIGEST_SIZE]);

// Lowercase hex, 2*len characters plus a NUL.  The wire format of both
// X-Signature and X-Nonce (dispenser-protocol.md).
void toHex(const uint8_t* bytes, size_t len, char* out);

// Constant-time equality over `len` bytes.  Not an optimisation: a comparison
// that returns on the first differing byte tells an attacker on the same WLAN
// how much of a guessed signature was right, one request at a time.  Whether
// that is exploitable across WiFi jitter is another question — this costs
// nothing, so the question does not have to be answered.
bool constantTimeEquals(const char* a, const char* b, size_t len);

#endif
