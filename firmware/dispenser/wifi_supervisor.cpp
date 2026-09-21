// firmware/dispenser/wifi_supervisor.cpp

#include "wifi_supervisor.h"

WifiSupervisor::WifiSupervisor(unsigned long restart_after_ms)
  : restart_after(restart_after_ms),
    was_connected(false),
    down_since_ms(0),
    reconnect_count(0) {
}

void WifiSupervisor::begin(bool connected, unsigned long now_ms) {
  was_connected = connected;
  down_since_ms = now_ms;
  reconnect_count = 0;
}

bool WifiSupervisor::update(bool connected, bool dispensing, unsigned long now_ms) {
  if (connected != was_connected) {
    if (connected) {
      // Down to up: the SDK's auto-reconnect did its job, or the join that
      // failed in setup() finally landed.  Both are reconnects as far as
      // anybody reading /health is concerned.
      reconnect_count++;
    } else {
      // Up to down: a fresh deadline, never the remainder of an older one.
      down_since_ms = now_ms;
    }
    was_connected = connected;
  }

  if (was_connected) {
    return false;
  }
  return shouldRestart(now_ms - down_since_ms, dispensing, restart_after);
}

bool WifiSupervisor::shouldRestart(unsigned long disconnected_ms, bool dispensing,
                                   unsigned long restart_after_ms) {
  if (dispensing) {
    return false;
  }
  return disconnected_ms > restart_after_ms;
}

unsigned long WifiSupervisor::disconnectedFor(unsigned long now_ms) const {
  if (was_connected) {
    return 0;
  }
  // Unsigned subtraction, so the difference is still right across the ~49.7
  // day millis() wraparound.  Anything that clamps or signs this hands the
  // supervisor a 49-day outage and restarts a healthy device.
  return now_ms - down_since_ms;
}
