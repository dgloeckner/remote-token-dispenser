# Native unit tests

These tests run on the host and exercise the **production** firmware classes.
There is deliberately no copy of any production class in this directory: the
sources listed in `build_src_filter` of `[env:native]` (`platformio.ini`) are
compiled into the test binary, and the mocks here implement the production
interfaces `IStorage` / `IHopper` (`../interfaces.h`).

Run them:

```sh
cd firmware/dispenser
pio test -e native
```

## Rules

- **Never re-declare a production class here.** If a class cannot be
  constructed from a test, give it an interface — that is what `interfaces.h`
  is for. A hand copy means a production bug can neither turn a test red nor a
  production fix turn one green; that was the state before issue #1.
- **A source file only counts as tested once it is in `build_src_filter`.**
  Anything that needs the ESP SDK (WiFi, EEPROM, async web server, GPIO) stays
  out of the native build; its behaviour is covered by the conformance suite
  (`dispenser-client-tui/`, `token-tui conformance`) instead.
- **`mocks/Arduino.h` grows only for Arduino-free production sources.** If a
  file needs more of the Arduino API than the handful of stubs there, that is
  a sign the logic belongs behind an interface.
- Tests whose name ends in `KNOWN_DEVIATION_issue_N` pin behaviour that the
  protocol says is wrong and that issue N will change. They assert what the
  firmware does *today* so CI stays honest; the issue that fixes the bug
  inverts the assertion and drops the suffix.
