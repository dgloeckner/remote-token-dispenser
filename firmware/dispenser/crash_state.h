// firmware/dispenser/crash_state.h
//
// Sealing and validation of the two things that must survive a reset: the
// flash record (active transaction + history ring) and the RTC block holding
// the live token count.  Arduino-free on purpose — this is the part of the
// crash handling that can be unit tested on the host, and it is listed in
// build_src_filter of [env:native].

#ifndef CRASH_STATE_H
#define CRASH_STATE_H

#include <stdint.h>
#include <stddef.h>

#include "dispenser_types.h"

// CRC-16/CCITT-FALSE.  Small, no table, and it catches the single-bit and
// truncation damage a half-finished EEPROM commit leaves behind.
uint16_t crc16Ccitt(const void* data, size_t length);

// Fill in magic, layout version and checksum.  Call before writing.
void sealPersistedRecord(PersistedRecord& record);

// True only for a record this build wrote and that is still intact.
bool persistedRecordValid(const PersistedRecord& record);

void sealRtcCountBlock(RtcCountBlock& block);

// True only for an intact block; the tx_id is compared by the caller, because
// a block from ANOTHER transaction is not a count for this one.
bool rtcCountBlockValid(const RtcCountBlock& block);

#endif
