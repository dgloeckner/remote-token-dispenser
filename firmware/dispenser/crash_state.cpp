// firmware/dispenser/crash_state.cpp

#include "crash_state.h"

#include <stddef.h>
#include <string.h>

// Same polynomial, resumable: the checksums below run over a struct with a
// hole in it (the checksum field itself), and copying a 200-byte record onto
// the stack just to zero four bytes is not worth it on an ESP8266 — the async
// web server's handlers are already on that stack.
static uint16_t crc16Update(uint16_t crc, const void* data, size_t length) {
  const uint8_t* bytes = (const uint8_t*)data;
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

// The checksum covers the whole record except the checksum field itself, so
// the same routine can compute it and check it.
static uint16_t persistedRecordChecksum(const PersistedRecord& record) {
  const uint8_t* bytes = (const uint8_t*)&record;
  const size_t hole = offsetof(PersistedRecord, crc16);
  uint16_t crc = crc16Update(0xFFFF, bytes, hole);
  return crc16Update(crc, bytes + hole + sizeof(record.crc16),
                     sizeof(record) - hole - sizeof(record.crc16));
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
  const uint8_t* bytes = (const uint8_t*)&block;
  const size_t hole = offsetof(RtcCountBlock, crc);
  uint16_t crc = crc16Update(0xFFFF, bytes, hole);
  crc = crc16Update(crc, bytes + hole + sizeof(block.crc),
                    sizeof(block) - hole - sizeof(block.crc));
  return (uint32_t)crc;
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
