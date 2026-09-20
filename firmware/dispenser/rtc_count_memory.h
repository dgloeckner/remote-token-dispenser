// firmware/dispenser/rtc_count_memory.h
//
// ICountMemory on ESP8266 RTC user memory.  Needs the ESP SDK, so it is not in
// the native build; the part that can be tested on the host (magic, checksum,
// what counts as valid) lives in crash_state.cpp and is.

#ifndef RTC_COUNT_MEMORY_H
#define RTC_COUNT_MEMORY_H

#include "dispenser_types.h"
#include "interfaces.h"

// Word offset inside the 512-byte RTC user memory.  The first 32 words are
// reserved by the SDK for OTA/deep-sleep bookkeeping on some cores; starting
// at 32 keeps out of their way.
#define RTC_COUNT_OFFSET 32

class RtcCountMemory : public ICountMemory {
public:
  bool readCount(const char* tx_id, uint8_t& out_count) override;
  void writeCount(const char* tx_id, uint8_t count) override;
  void invalidate() override;
};

#endif
