// firmware/dispenser/pulse_filter.h
//
// Which falling edges on the coin line are tokens.
//
// The coin sensor is an optocoupler output, and the motor it shares a supply
// with switches inductive load a few centimetres away.  Both produce edges
// that are not coins: a bouncing sensor gives a burst of them inside a few
// milliseconds, EMI gives single spikes.  The ISR used to count every one
// (issue #5), so noise reached the target count early, the ISR-level stop
// fired, and the member got fewer tokens than the terminal billed — with
// nothing anywhere saying so.
//
// The rule is spacing, and the spacing comes from the hopper datasheet: one
// coin is one LOW phase of 30-65 ms and coins arrive about a second apart, so
// two edges closer together than COIN_PULSE_MIN_GAP_MS (config.h) cannot both
// be coins.  The gap is measured against the last ACCEPTED edge, not the last
// edge seen — otherwise a burst of noise walks the deadline forward with it
// and the real pulse behind it is swallowed.
//
// Deliberately free of Arduino/ESP SDK includes so the native test build
// compiles the real class (firmware/dispenser/test/README.md).
//
// What this class does NOT do, on purpose: confirm the pulse WIDTH on the
// rising edge.  It would need a CHANGE interrupt and, worse, it could only
// count a token 30-65 ms after it fell — which is exactly the window in which
// the ISR-level motor stop has to happen.  Trading a late stop (one more
// token out of the hopper) for a width check would undo commit a9f15af.

#ifndef PULSE_FILTER_H
#define PULSE_FILTER_H

#include <stdint.h>

class PulseFilter {
public:
  // min_gap_us is the spacing below which an edge is noise.  It is a
  // constructor argument and not a compile-time constant so the tests can
  // drive the boundary directly; production passes COIN_PULSE_MIN_GAP_MS.
  explicit PulseFilter(uint32_t min_gap_us);

  // Forget the last edge.  Called between transactions, from loop() context.
  void reset();

  // One falling edge, timestamped with micros().  true means "this is a
  // token", false means "this was noise".  Called from the ISR, so the
  // definition carries IRAM_ATTR on the device.
  //
  // uint32_t subtraction is used throughout: micros() wraps every ~71 minutes
  // and the wrapped difference is still the true elapsed time.
  bool accept(uint32_t now_us);

  uint32_t accepted() const { return accepted_count; }
  // Edges thrown away since the last reset().  Diagnostics: a hopper that
  // bounces reports here instead of quietly shortening every dispense.
  uint32_t rejected() const { return rejected_count; }

private:
  const uint32_t min_gap_us;
  volatile uint32_t last_accepted_us;
  volatile bool have_last;
  volatile uint32_t accepted_count;
  volatile uint32_t rejected_count;
};

#endif
