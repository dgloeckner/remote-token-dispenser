// firmware/dispenser/flash_storage.h

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <Arduino.h>
#include "dispenser_types.h"
#include "interfaces.h"

// TransactionState, PersistedTransaction and PersistedRecord live in
// dispenser_types.h so that the native test build can use them without the
// ESP SDK.

class FlashStorage : public IStorage {
public:
  void begin() override;
  bool load(PersistedRecord& out) override;
  void save(const PersistedRecord& record) override;
  void clear() override;
};

#endif
