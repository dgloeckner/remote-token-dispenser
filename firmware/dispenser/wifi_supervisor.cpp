// firmware/dispenser/wifi_supervisor.cpp
//
// TODAY'S BEHAVIOUR, on purpose: the firmware has no recovery from a lost or
// failed join and counts no reconnects, so this stub says "never restart" and
// "zero reconnects".  The assertions in test/test_dispense_manager.cpp are red
// against it; the next commit is the rule itself.

#include "wifi_supervisor.h"

WifiSupervisor::WifiSupervisor(unsigned long restart_after_ms)
  : restart_after(restart_after_ms),
    was_connected(false),
    down_since_ms(0),
    reconnect_count(0) {
}

void WifiSupervisor::begin(bool connected, unsigned long now_ms) {
  (void)connected;
  (void)now_ms;
}

bool WifiSupervisor::update(bool connected, bool dispensing, unsigned long now_ms) {
  (void)connected;
  (void)dispensing;
  (void)now_ms;
  return false;
}

bool WifiSupervisor::shouldRestart(unsigned long disconnected_ms, bool dispensing,
                                   unsigned long restart_after_ms) {
  (void)disconnected_ms;
  (void)dispensing;
  (void)restart_after_ms;
  return false;
}

unsigned long WifiSupervisor::disconnectedFor(unsigned long now_ms) const {
  (void)now_ms;
  return 0;
}
