// firmware/dispenser/request_signer.cpp — see request_signer.h.

#include "request_signer.h"
#include <string.h>

namespace {

const char LF = '\n';

// The canonical string is fed to the HMAC in pieces rather than assembled in a
// buffer: the body is up to REQUEST_BODY_CAPACITY bytes and the device has no
// heap to spare for a second copy of it.
void feed(Sha256& h, const char* s, size_t len) {
  if (len > 0) {
    h.update((const uint8_t*)s, len);
  }
}

}  // namespace

void RequestSigner::sign(const char* key, const char* method, const char* path,
                         const char* body, size_t bodyLen, const char* nonce,
                         char* out) {
  // HMAC(key, METHOD \n PATH \n BODY \n NONCE).  Written out by hand rather
  // than through hmacSha256() because the message is four pieces and one of
  // them can be 256 bytes; the inner hash is streamed instead of concatenated.
  uint8_t k[SHA256_BLOCK_SIZE];
  memset(k, 0, sizeof(k));
  size_t keyLen = strlen(key);
  if (keyLen > SHA256_BLOCK_SIZE) {
    Sha256 kh;
    kh.update((const uint8_t*)key, keyLen);
    kh.finish(k);
  } else {
    memcpy(k, key, keyLen);
  }

  uint8_t pad[SHA256_BLOCK_SIZE];
  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++) {
    pad[i] = k[i] ^ 0x36;
  }

  Sha256 inner;
  inner.update(pad, SHA256_BLOCK_SIZE);
  feed(inner, method, strlen(method));
  inner.update((const uint8_t*)&LF, 1);
  feed(inner, path, strlen(path));
  inner.update((const uint8_t*)&LF, 1);
  feed(inner, body, bodyLen);
  inner.update((const uint8_t*)&LF, 1);
  feed(inner, nonce, strlen(nonce));

  uint8_t innerDigest[SHA256_DIGEST_SIZE];
  inner.finish(innerDigest);

  for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++) {
    pad[i] = k[i] ^ 0x5c;
  }

  Sha256 outer;
  outer.update(pad, SHA256_BLOCK_SIZE);
  outer.update(innerDigest, SHA256_DIGEST_SIZE);

  uint8_t digest[SHA256_DIGEST_SIZE];
  outer.finish(digest);
  toHex(digest, SHA256_DIGEST_SIZE, out);

  memset(k, 0, sizeof(k));
  memset(pad, 0, sizeof(pad));
}

RequestSigner::RequestSigner(const char* key)
  : signing_key(key), next_slot(0) {
  memset(pool, 0, sizeof(pool));
}

bool RequestSigner::expired(unsigned long issued_ms, unsigned long now_ms) {
  // Unsigned subtraction, so the 49-day millis() wraparound is not a special
  // case: an outstanding nonce spans at most 30 s of it.
  return (unsigned long)(now_ms - issued_ms) > NONCE_TTL_MS;
}

void RequestSigner::issueNonce(unsigned long now_ms, uint32_t w0, uint32_t w1,
                               uint32_t w2, uint32_t w3, char* out) {
  uint8_t bytes[16];
  const uint32_t words[4] = {w0, w1, w2, w3};
  for (int i = 0; i < 4; i++) {
    bytes[i * 4]     = (uint8_t)(words[i] >> 24);
    bytes[i * 4 + 1] = (uint8_t)(words[i] >> 16);
    bytes[i * 4 + 2] = (uint8_t)(words[i] >> 8);
    bytes[i * 4 + 3] = (uint8_t)(words[i]);
  }
  toHex(bytes, 16, out);

  // Prefer a free or expired slot; otherwise evict round-robin, which with a
  // fixed pool and a fixed TTL is the oldest.
  int slot = -1;
  for (int i = 0; i < NONCE_POOL_SIZE; i++) {
    if (!pool[i].used || expired(pool[i].issued_ms, now_ms)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    slot = next_slot;
    next_slot = (uint8_t)((next_slot + 1) % NONCE_POOL_SIZE);
  }

  memcpy(pool[slot].nonce, out, NONCE_HEX_LEN + 1);
  pool[slot].issued_ms = now_ms;
  pool[slot].used = true;
  pool[slot].spent = false;
}

int RequestSigner::findSlot(const char* nonce, unsigned long now_ms) const {
  for (int i = 0; i < NONCE_POOL_SIZE; i++) {
    if (!pool[i].used || expired(pool[i].issued_ms, now_ms)) {
      continue;
    }
    if (strncmp(pool[i].nonce, nonce, NONCE_HEX_LEN + 1) == 0) {
      return i;
    }
  }
  return -1;
}

int RequestSigner::liveNonces(unsigned long now_ms) const {
  int live = 0;
  for (int i = 0; i < NONCE_POOL_SIZE; i++) {
    if (pool[i].used && !expired(pool[i].issued_ms, now_ms)) {
      live++;
    }
  }
  return live;
}

AuthResult RequestSigner::verify(unsigned long now_ms, const char* method,
                                 const char* path, const char* body,
                                 size_t bodyLen, const char* nonce,
                                 const char* signature, bool mutating) {
  // No headers at all is a bad signature, not a bad nonce: a client that sends
  // neither has not been told to sign, and telling it to fetch a nonce and try
  // again would put it in a loop.
  if (nonce == NULL || signature == NULL) {
    return AUTH_BAD_SIGNATURE;
  }
  if (strlen(signature) != SIGNATURE_HEX_LEN) {
    return AUTH_BAD_SIGNATURE;
  }
  if (strlen(nonce) != NONCE_HEX_LEN) {
    return AUTH_BAD_NONCE;
  }

  int slot = findSlot(nonce, now_ms);
  if (slot < 0) {
    return AUTH_BAD_NONCE;
  }

  // The signature is checked BEFORE the nonce is spent, and the "already
  // spent" answer comes after it.  Otherwise an attacker with no key could
  // burn a terminal's nonce by replaying its bytes with one character
  // changed.
  char expected[SIGNATURE_HEX_LEN + 1];
  sign(signing_key, method, path, body, bodyLen, nonce, expected);
  if (!constantTimeEquals(expected, signature, SIGNATURE_HEX_LEN)) {
    return AUTH_BAD_SIGNATURE;
  }

  if (mutating) {
    if (pool[slot].spent) {
      // The replay this whole scheme exists to stop: the identical POST, the
      // identical signature, a second time.
      return AUTH_BAD_NONCE;
    }
    pool[slot].spent = true;
  }

  return AUTH_OK;
}
