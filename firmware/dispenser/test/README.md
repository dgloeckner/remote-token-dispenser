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
- **A mock symbol is declared in `mocks/`, but defined in `arduino_mock.cpp`
  here in the test root.** PlatformIO compiles the sources of the test suite
  itself, not those in its subdirectories, so a definition parked next to its
  header is never built and the link ends in `undefined reference`. Defining
  it in the header instead only trades that for a duplicate symbol.
- **A reboot is a new manager over the mocks that survived it.** The `reboot()`
  helper in `test_dispense_manager.cpp` throws the DispenseManager away and
  builds a new one on the same `FlashStorageMock` and `CountMemoryMock` — which
  is what a reset looks like from the manager's side. Which reset it was is one
  call: `counts->loseRtcMemory()` is a power loss, doing nothing is a watchdog
  reset. Both cases matter (issue #3), so both are tested.
- **A POST is two steps, not one.** Since #4 `requestDispense()` only decides
  and fills the request slot; the flash commit and the motor start happen in
  the next `loop()` pass. The `startAndRun()` helper in
  `test_dispense_manager.cpp` is both halves, and a test that wants a *running*
  transaction uses it. The tests that assert the split itself (the four under
  "The request slot") deliberately call `requestDispense()` and `loop()`
  separately — that is the whole point of them.
- **A transaction is not finished when the count is reached.** Since #5 the
  target count stops the motor and opens the 500 ms settling window; the
  transaction reaches `STATE_DONE` only in a later `loop()` pass. The
  `loopUntilDone()` helper in `test_dispense_manager.cpp` is that pair — the
  completing pass and the clock advance after it. The tests that assert the
  window itself do the two steps by hand, which is the point of them.
- Tests whose name ends in `KNOWN_DEVIATION_issue_N` pin behaviour that the
  protocol says is wrong and that issue N will change. They assert what the
  firmware does *today* so CI stays honest; the issue that fixes the bug
  inverts the assertion and drops the suffix.
