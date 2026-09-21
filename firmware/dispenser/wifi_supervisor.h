// firmware/dispenser/wifi_supervisor.h
//
// The connectivity supervisor of issue #7.
//
// Deliberately Arduino-free: it is fed facts (is the link up, is the motor
// running, what time is it) and answers one question — may the device restart
// itself now?  That keeps the decision in the native test build; the two SDK
// calls it implies (`WiFi.status()` and `ESP.restart()`) stay in the sketch.

#ifndef WIFI_SUPERVISOR_H
#define WIFI_SUPERVISOR_H

#include <stdint.h>

// How long the link may be down before the device restarts itself.
//
// A failed join used to be final: setup() waited 15 s, logged "WiFi connection
// failed" and started the HTTP server on a device nobody could reach.  The
// SDK's auto-reconnect covers the ordinary case; it does not cover an AP that
// comes back on another channel or a stale DHCP lease, and nothing else in the
// sketch ever noticed.  One minute is long enough that a normal roam or a
// router reboot is ridden out by the SDK, and short enough that the terminal's
// next customer finds a working machine.
#define WIFI_RESTART_AFTER_MS 60000UL

class WifiSupervisor {
public:
  explicit WifiSupervisor(unsigned long restart_after_ms = WIFI_RESTART_AFTER_MS);

  // The state the sketch starts from, after setup()'s join attempt has had its
  // 15 s.  A failed join therefore starts the clock here, which is the point:
  // the device that never joined is exactly the one nobody can ask to reboot.
  void begin(bool connected, unsigned long now_ms);

  // One loop() pass.  Returns true when the device should restart NOW.
  bool update(bool connected, bool dispensing, unsigned long now_ms);

  // The rule itself, as a pure function: down for longer than the deadline and
  // not dispensing.  **Never while the motor is on** — a restart mid-dispense
  // would turn a working transaction into a RESET error and leave tokens on
  // the floor of the accounting.  The device is unreachable either way; the
  // customer standing in front of it is not.
  static bool shouldRestart(unsigned long disconnected_ms, bool dispensing,
                            unsigned long restart_after_ms = WIFI_RESTART_AFTER_MS);

  // Transitions from down to up since begin(), reported as `wifi.reconnects`
  // by GET /health.  A device that reconnects ten times a night has a WiFi
  // problem that uptime alone never shows.
  uint16_t reconnects() const { return reconnect_count; }

  bool connected() const { return was_connected; }

  // How long the link has been down, 0 while it is up.
  unsigned long disconnectedFor(unsigned long now_ms) const;

private:
  unsigned long restart_after;
  bool was_connected;
  unsigned long down_since_ms;
  uint16_t reconnect_count;
};

#endif
