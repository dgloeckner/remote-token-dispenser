// firmware/dispenser/crash_state.cpp

#include "crash_state.h"

#include <string.h>

uint16_t crc16Ccitt(const void* data, size_t length) {
  const uint8_t* bytes = (const uint8_t*)data;
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= (uint16_t)bytes[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

// The checksum covers the whole record with the checksum field itself zeroed,
// so the same routine can compute it and check it.
static uint16_t persistedRecordChecksum(const PersistedRecord& record) {
  PersistedRecord copy = record;
  copy.crc16 = 0;
  return crc16Ccitt(&copy, sizeof(copy));
}

void sealPersistedRecord(PersistedRecord& record) {
  record.magic = PERSIST_MAGIC;
  record.layout_version = PERSIST_LAYOUT_VERSION;
  record.reserved[0] = 0;
  record.reserved[1] = 0;
  record.reserved[2] = 0;
  record.crc16 = persistedRecordChecksum(record);
}

bool persistedRecordValid(const PersistedRecord& record) {
  if (record.magic != PERSIST_MAGIC) {
    return false;
  }
  if (record.layout_version != PERSIST_LAYOUT_VERSION) {
    return false;
  }
  if (record.ring_index >= PERSIST_RING_SIZE) {
    return false;
  }
  return record.crc16 == persistedRecordChecksum(record);
}

static uint32_t rtcCountBlockChecksum(const RtcCountBlock& block) {
  RtcCountBlock copy = block;
  copy.crc = 0;
  return (uint32_t)crc16Ccitt(&copy, sizeof(copy));
}

void sealRtcCountBlock(RtcCountBlock& block) {
  block.magic = RTC_COUNT_MAGIC;
  block.crc = rtcCountBlockChecksum(block);
}

bool rtcCountBlockValid(const RtcCountBlock& block) {
  if (block.magic != RTC_COUNT_MAGIC) {
    return false;
  }
  if (block.tx_id[sizeof(block.tx_id) - 1] != '\0') {
    return false;
  }
  return block.crc == rtcCountBlockChecksum(block);
}
