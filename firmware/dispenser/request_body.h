// firmware/dispenser/request_body.h
//
// Assembling a request body that arrives in pieces.
//
// ESPAsyncWebServer hands the body callback one chunk at a time, with the
// chunk's `index` and the body's `total`.  The POST handler used to parse
// whatever chunk it was given, so a body split across two TCP segments was
// answered `400 invalid json` — at random, because the split depends on the
// network, not on the request (issue #4).
//
// Deliberately free of Arduino/ESP SDK includes: this is the one half of the
// HTTP layer that can be unit-tested on the host, and it is in the native
// build_src_filter so that it is.

#ifndef REQUEST_BODY_H
#define REQUEST_BODY_H

#include <stdint.h>
#include <stddef.h>

// How much of a body is accepted.  A dispense request is about 40 bytes; 256
// is room for a generous one and nothing more.  Beyond it the request is
// refused with 413 instead of being truncated into a parse error that blames
// the wrong thing.
#define REQUEST_BODY_CAPACITY 256

enum BodyStatus {
  BODY_INCOMPLETE = 0,  // more chunks are coming
  BODY_COMPLETE = 1,    // all `total` bytes are in, parse it
  BODY_TOO_LARGE = 2,   // more than REQUEST_BODY_CAPACITY → 413
  BODY_MALFORMED = 3    // a chunk that does not fit the stream → 400
};

// Plain data on purpose: the async server owns this object through its
// `_tempObject` slot, which it allocates with malloc and releases with free.
// reset() is the constructor, and it is always called right after the malloc.
class RequestBody {
public:
  void reset();

  // Takes one chunk as the body callback receives it.  Returns what the
  // handler should do; once TOO_LARGE or MALFORMED is returned, that verdict
  // stands for the rest of this body.
  BodyStatus append(const uint8_t* data, size_t len, size_t index, size_t total);

  // Null-terminated, valid once append() returned BODY_COMPLETE.
  const char* data() const { return buffer; }
  size_t size() const { return used; }
  bool isEmpty() const { return used == 0; }
  BodyStatus status() const { return verdict; }

private:
  char buffer[REQUEST_BODY_CAPACITY + 1];
  size_t used;
  BodyStatus verdict;
};

#endif
