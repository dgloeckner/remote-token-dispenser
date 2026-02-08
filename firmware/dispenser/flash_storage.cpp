// firmware/dispenser/flash_storage.cpp

#include "flash_storage.h"
#include <EEPROM.h>

#define EEPROM_SIZE 512
#define MAGIC_BYTE 0xAB  // Indicates valid data
#define ADDR_MAGIC 0
#define ADDR_DATA 1

void FlashStorage::begin() {
  EEPROM.begin(EEPROM_SIZE);
}

bool FlashStorage::hasPersistedTransaction() {
  return EEPROM.read(ADDR_MAGIC) == MAGIC_BYTE;
}

PersistedTransaction FlashStorage::load() {
  PersistedTransaction tx;

  if (!hasPersistedTransaction()) {
    // Return empty transaction
    memset(&tx, 0, sizeof(tx));
    tx.state = STATE_IDLE;
    return tx;
  }

  // Read from EEPROM
  EEPROM.get(ADDR_DATA, tx);
  return tx;
}

void FlashStorage::persist(const PersistedTransaction& tx) {
  // Write magic byte
  EEPROM.write(ADDR_MAGIC, MAGIC_BYTE);

  // Write transaction data
  EEPROM.put(ADDR_DATA, tx);

  // Commit to flash
  EEPROM.commit();
}

void FlashStorage::clear() {
  EEPROM.write(ADDR_MAGIC, 0x00);
  EEPROM.commit();
}
