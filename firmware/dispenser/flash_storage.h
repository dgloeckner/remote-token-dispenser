// firmware/dispenser/flash_storage.h

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>
#include "dispenser_types.h"
#include "interfaces.h"

// TransactionState and PersistedTransaction live in dispenser_types.h so that
// the native test build can use them without the ESP SDK.

class FlashStorage : public IStorage {
public:
  void begin() override;
  bool hasPersistedTransaction() override;
  PersistedTransaction load() override;
  void persist(const PersistedTransaction& tx) override;
  void clear() override;
};

#endif
