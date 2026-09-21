// firmware/dispenser/request_body.cpp

#include "request_body.h"

#include <string.h>

void RequestBody::reset() {
  used = 0;
  verdict = BODY_INCOMPLETE;
  buffer[0] = '\0';
}

BodyStatus RequestBody::append(const uint8_t* data, size_t len,
                               size_t index, size_t total) {
  // A first chunk starts a fresh body.  The async server reuses nothing, but
  // saying so here means the object can also be reused, and a stray index 0
  // after a verdict cannot resurrect a refused body silently.
  if (index == 0) {
    reset();
  }

  if (verdict == BODY_TOO_LARGE || verdict == BODY_MALFORMED) {
    return verdict;  // the verdict stands for the rest of this body
  }

  if (total > REQUEST_BODY_CAPACITY) {
    verdict = BODY_TOO_LARGE;
    return verdict;
  }

  // The chunks of one body arrive in order and without gaps.  Anything else is
  // a stream we did not follow, and guessing at it would mean parsing bytes in
  // the wrong order.
  if (index != used) {
    verdict = BODY_MALFORMED;
    return verdict;
  }

  if (used + len > REQUEST_BODY_CAPACITY) {
    verdict = BODY_TOO_LARGE;
    return verdict;
  }

  if (len > 0 && data != NULL) {
    memcpy(buffer + used, data, len);
    used += len;
  }
  buffer[used] = '\0';

  if (used >= total) {
    verdict = BODY_COMPLETE;
  }
  return verdict;
}
