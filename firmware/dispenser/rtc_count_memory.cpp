// firmware/dispenser/rtc_count_memory.cpp

#include "rtc_count_memory.h"

#include <Arduino.h>
#include <string.h>

#include "crash_state.h"

bool RtcCountMemory::readCount(const char* tx_id, uint8_t& out_count) {
  RtcCountBlock block;
  memset(&block, 0, sizeof(block));
  if (!ESP.rtcUserMemoryRead(RTC_COUNT_OFFSET, (uint32_t*)&block, sizeof(block))) {
    return false;
  }
  if (!rtcCountBlockValid(block)) {
    return false;
  }
  // A block belonging to some earlier transaction is not a count for this one.
  if (strncmp(block.tx_id, tx_id, sizeof(block.tx_id) - 1) != 0) {
    return false;
  }
  if (block.dispensed > 0xFF) {
    return false;
  }
  out_count = (uint8_t)block.dispensed;
  return true;
}

void RtcCountMemory::writeCount(const char* tx_id, uint8_t count) {
  RtcCountBlock block;
  memset(&block, 0, sizeof(block));
  strncpy(block.tx_id, tx_id, sizeof(block.tx_id) - 1);
  block.dispensed = count;
  sealRtcCountBlock(block);
  ESP.rtcUserMemoryWrite(RTC_COUNT_OFFSET, (uint32_t*)&block, sizeof(block));
}

void RtcCountMemory::invalidate() {
  RtcCountBlock block;
  memset(&block, 0, sizeof(block));
  // No magic, no checksum: whatever reads this next sees an empty block.
  ESP.rtcUserMemoryWrite(RTC_COUNT_OFFSET, (uint32_t*)&block, sizeof(block));
}
