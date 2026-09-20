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

// TODO(issue #5): this is today's behaviour — every edge is a token.
// IRAM_ATTR because the coin-pulse ISR calls it: a function left in flash is
// a cache miss the interrupt cannot take.
bool IRAM_ATTR PulseFilter::accept(uint32_t now_us) {
  last_accepted_us = now_us;
  have_last = true;
  accepted_count++;
  return true;
}
