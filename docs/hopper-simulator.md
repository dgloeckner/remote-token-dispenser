# Hopper simulator (second D1 mini)

A second Wemos D1 mini that pretends to be the Azkoyen Hopper U-II: it watches
the dispenser's motor line and answers with coin pulses. It exists so that the
tests that used to need a person, a jam and a bag of tokens run unattended.

Sketch: `firmware/hopper-simulator/hopper-simulator.ino`.
It replaces the ad-hoc error-pulse sketch that used to live in
[`triggering-test-errors.md`](triggering-test-errors.md); that one only forged
the error line.

## What it can do

| Mode | Command | What the dispenser sees | What it is for |
|------|---------|--------------------------|----------------|
| normal | `n` | one clean 30 ms pulse per token | the happy path |
| bounce burst | `b` | 3 edges within ~5 ms, then the real pulse | a dispenser that counts raw falling edges counts 4 tokens; issue #5 |
| coast pulse | `c` | one extra pulse 120 ms **after** the motor stops | the token that slips out after the ISR stop; issue #5 |
| jam after N | `jN` (e.g. `j3`) | pulses stop after N tokens, motor keeps running | jam timeout, partial count, fault handling; issues #3, #6 |
| error code N | `eN` (e.g. `e5`) | Azkoyen error pulse train on the error line | error decoding and the fault state; issue #6 |
| fast mode | `f` | 300 ms per token instead of 1000 ms | soak runs (`--soak`, issue #7) |
| status | `s` | — | prints mode, motor state and token counts |
| reset | `r` | — | back to normal mode, counters zeroed |

Commands go over the serial monitor at **115200 baud**. They are single
characters (plus a number where one is needed), so a prompt from the
conformance suite can be followed literally.

## Soak runs (Cycle A)

Fast mode plus the soak runner is the epic's Cycle A, and it is the one test
that can find a leak, a nightly reset or a radio that fell asleep:

```sh
# in the simulator's serial monitor
f

# on the workstation
token-tui conformance --endpoint http://192.168.4.20 --signing-key … \
  --target simulator --soak 200 --json report-A.json
```

Roughly 200 × (300 ms + the 500 ms settling window + a poll), so about ten
minutes. It runs instead of the case table — run the table first, then power
the dispenser off and on, then soak: the table ends with the destructive fault
cases, and a faulted device refuses every dispense after them. What the run
asserts is in `dispenser-protocol.md` under *Conformance → Soak runs*.

## Wiring

Three signal lines and a common ground, 3.3 V logic on both sides. The
optocouplers belong between the dispenser and the *real* hopper; between the
two boards they are neither needed nor wanted.

| Simulator (D1 mini #2) | Direction | Dispenser (D1 mini #1) | Meaning |
|------------------------|-----------|------------------------|---------|
| `D2` (GPIO4) | ← | `D1` (GPIO5) | motor control; HIGH = motor on |
| `D5` (GPIO14) | → | `D7` (GPIO13) | coin pulse, active LOW |
| `D6` (GPIO12) | → | `D5` (GPIO14) | hopper error signal, active LOW |
| `GND` | — | `GND` | common reference — without it nothing works |

```
   D1 mini #2 (simulator)              D1 mini #1 (dispenser firmware)
   ┌──────────────────┐                ┌──────────────────┐
   │ D2  motor watch  │◄───────────────│ D1  motor control│
   │ D5  coin pulse   │───────────────►│ D7  coin pulse   │
   │ D6  error signal │───────────────►│ D5  error signal │
   │ GND              │────────────────│ GND              │
   └──────────────────┘                └──────────────────┘
        USB power                           USB power
```

**Disconnect the hopper while the simulator is wired up.** Two drivers on one
input line is a short waiting to happen, and the hopper's side runs at 12 V.

## Flashing

```sh
cd firmware/hopper-simulator
pio run -e simulator -t upload      # the SECOND board, not the dispenser
pio device monitor -b 115200
```

## Using it with the conformance suite

```sh
# unattended: everything that does not need a physical act
token-tui conformance --endpoint http://192.168.4.20 --signing-key … --target simulator

# with the cases that prompt for RST, power or a simulator switch
token-tui conformance --endpoint http://192.168.4.20 --signing-key … \
  --target simulator --interactive --json report.json
```

Set the simulator to fast mode (`f`) first; a 20-token case otherwise takes
20 seconds of real hopper time.

Two interactive cases drive the switches of issue #5, and the prompt names the
command to type:

| Case | Command | What it proves |
|------|---------|----------------|
| `bounce_burst_counts_one_token_per_coin` | `b` | the pulse filter: three noise edges plus the real pulse are **one** token, so the dispense is not cut short |
| `coast_pulse_is_counted_as_an_overrun` | `c` | the settling window: the token that falls 120 ms after the motor stop is counted and reported, so `dispensed` is `quantity + 1` |

Both end by prompting for `n` again — a simulator left in bounce mode makes
every later case fail for the wrong reason.

## Limits

- It models the **signals**, not the mechanics: no coin weight, no motor
  current, no slipping disc. A pulse it does not send is a token that never
  existed, so it cannot reproduce miscounts caused by the sensor optics.
- It answers the motor line from `loop()`, so its pulse edges are accurate to
  about a millisecond — fine against the 30 ms hopper pulse, useless for
  timing experiments below that.
- The coast pulse is fixed at 120 ms after the motor stop (`COAST_DELAY_MS`),
  so it only exercises the near edge of the firmware's 500 ms settling window.
  A token that coasts out later than the window is not reproducible here, and
  would be reported as an overrun of the *next* transaction or not at all.
  That is a deliberate gap: the number that matters is measured on the real
  hopper (Cycle B of the epic's plan), not on a board that invents it.
- Fast mode is faster than the real hopper. A timing-dependent bug that only
  shows at 1000 ms/token will not show at 300 ms/token; leave fast mode off
  when chasing one.
