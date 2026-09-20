// firmware/dispenser/flash_storage.cpp
//
// One record, one commit.  There is no separate magic byte any more: magic,
// layout version and checksum sit inside the record itself, so a half-written
// commit is detected rather than read back as data (issue #3).

#include "flash_storage.h"
#include <EEPROM.h>

#include "crash_state.h"

#define EEPROM_SIZE 512
#define ADDR_RECORD 0

void FlashStorage::begin() {
  EEPROM.begin(EEPROM_SIZE);
}

bool FlashStorage::load(PersistedRecord& out) {
  PersistedRecord stored;
  memset(&stored, 0, sizeof(stored));
  EEPROM.get(ADDR_RECORD, stored);

  if (!persistedRecordValid(stored)) {
    return false;
  }

  out = stored;
  return true;
}

void FlashStorage::save(const PersistedRecord& record) {
  PersistedRecord sealed = record;
  sealPersistedRecord(sealed);
  EEPROM.put(ADDR_RECORD, sealed);
  EEPROM.commit();
}

void FlashStorage::clear() {
  // Destroying the magic is enough: load() then reports "nothing usable".
  uint32_t dead = 0;
  EEPROM.put(ADDR_RECORD, dead);
  EEPROM.commit();
}
