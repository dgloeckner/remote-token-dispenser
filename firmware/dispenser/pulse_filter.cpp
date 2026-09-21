// firmware/dispenser/pulse_filter.cpp

#include "pulse_filter.h"

#ifndef IRAM_ATTR
  #define IRAM_ATTR
#endif

PulseFilter::PulseFilter(uint32_t gap_us)
  : min_gap_us(gap_us), last_accepted_us(0), have_last(false),
    accepted_count(0), rejected_count(0) {}

void PulseFilter::reset() {
  have_last = false;
  last_accepted_us = 0;
  accepted_count = 0;
  rejected_count = 0;
}

// IRAM_ATTR because the coin-pulse ISR calls it: a function left in flash is a
// cache miss the interrupt cannot take.
//
// The subtraction is deliberately done in uint32_t: micros() wraps every ~71
// minutes, and the wrapped difference is still the elapsed time.  A signed
// comparison against a stored timestamp would read the wrap as a gap of half
// an hour in the wrong direction and let a burst of noise through.
bool IRAM_ATTR PulseFilter::accept(uint32_t now_us) {
  if (have_last && (uint32_t)(now_us - last_accepted_us) < min_gap_us) {
    // Measured against the last ACCEPTED edge, never the last edge seen: a
    // bouncing sensor would otherwise push the deadline ahead of itself and
    // swallow the real pulse at the end of its own burst.
    rejected_count++;
    return false;
  }
  last_accepted_us = now_us;
  have_last = true;
  accepted_count++;
  return true;
}
