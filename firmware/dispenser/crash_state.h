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

// Both blocks are checked with CRC-16/CCITT-FALSE: small, no table, and it
// catches the single-bit and truncation damage a half-finished EEPROM commit
// leaves behind.  The routine itself is private to crash_state.cpp — what the
// rest of the firmware needs is "seal this" and "is this intact".

// Fill in magic, layout version and checksum.  Call before writing.
void sealPersistedRecord(PersistedRecord& record);

// True only for a record this build wrote and that is still intact.
bool persistedRecordValid(const PersistedRecord& record);

void sealRtcCountBlock(RtcCountBlock& block);

// True only for an intact block; the tx_id is compared by the caller, because
// a block from ANOTHER transaction is not a count for this one.
bool rtcCountBlockValid(const RtcCountBlock& block);

#endif
