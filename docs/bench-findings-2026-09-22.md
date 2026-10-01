# Bench findings — 2026-09-22

Bring-up of the prototype after several months idle. Wiring was known to be
incomplete; the goal of the session was to find out, piece by piece, what
still works. Firmware `1d25c20` (`1.1.0-DEBUG-error-decoding`), rebuilt and
re-flashed during the session; TUI rebuilt from the same tree.

## Setup as found

| Item | State |
|---|---|
| ESP8266 (Wemos D1 mini) | Reachable at `192.168.188.243` (static, `config.local.h`), SSID `Ponyhof`, RSSI −52…−55 |
| USB serial on the Mac | `/dev/cu.usbserial-1120` (first cable was charge-only; `arduino-cli board list --watch` finds it) |
| Optocouplers | Three in use: D1 motor, D7 coin, D5 error. D6 (empty sensor) **not connected** |
| Hopper connector 6 (logic & mode) | 2×3 pin header, **no jumper caps** — see photo `hopper-connector6-2026-09-22.jpg` |
| Ribbon cable | Was plugged in; pulled once for the coin-line test, then re-plugged |

## What was verified

1. **Firmware build + upload** works via `arduino-cli`
   (`compile`/`upload --fqbn esp8266:esp8266:d1_mini dispenser/`).
   `pio run -e esp8266` fails with *"Nothing to build"* because the sources
   are in the sketch root, not `src/` (goes into
   [remote-token-dispenser#1](https://github.com/dgloeckner/remote-token-dispenser/issues/1)).
   Serial monitor is **9600 baud** (`dispenser.ino`), not the 115200 the
   README says.
2. **ESP side of the coin channel is fine.** With the ribbon cable unplugged,
   D7 reads `raw=1, active=false` and the optocoupler LED is off.
3. **Hopper mode is correct.** The manufacturer figure 5 (`hopper-protocol.pdf`
   p. 5) shows connector 6 as a jumper header where STANDARD + NEGATIVE — the
   mode the firmware needs — is **no jumper at all**. The photo shows exactly
   that. If a jumper was pulled from this header at some point, that was the
   right move (a cap in the middle column = POSITIVE = inverted motor logic).
4. **API key** in `config.local.h` is the one the device accepts (a POST with it
   → 200; the `config.h` placeholder → 401). A 401 in the TUI means the key
   did not reach it — start it with
   `TOKEN_DISPENSER_API_KEY="$(sed -n 's/.*API_KEY "\(.*\)"/\1/p' ../firmware/dispenser/config.local.h)" ./token-tui --endpoint http://192.168.188.243`.

## Open problems

### A. Coin line (hopper pin 9 → PC817 #2 → D7) is LOW at rest

With the ribbon cable plugged in and the hopper powered: D7 optocoupler LED
**on** continuously, `/health` → `coin_pulse raw=0, active=true`. The hopper's
coin output is an open collector that is high-impedance at rest and pulls low
only for 30–65 ms per coin, so at rest the LED must be **off**. A line that is
already low can never produce the falling edge the ISR counts → every dispense
ends in the 5 s jam timeout with `dispensed=0`, which is what happened
(tx `4b370499`, 20:47).

The low level comes through the ribbon cable (cable out → LED off). Not yet
separated:

| Cause | LED at rest | LED with exit sensor blocked by a token |
|---|---|---|
| Correct wiring, nothing stuck | off | on |
| Coin pin on IN+, GND on IN− (swapped) | on | off |
| Token/debris in the exit sensor | on | on |
| Wire on the wrong hopper pin (see C) | on | on |

**Next step:** cable in, nothing in the chute, block the exit sensor by hand,
watch the D7 LED.

### B. Motor does not engage

Quantity-1 dispense at 20:47:42: POST accepted (200, 616 ms), no motor sound,
no pulse, jam after 5 s. No hopper error decoded (`error_history` empty), so
the hopper itself reported nothing. In NEGATIVE logic *disconnected = stop*, so
"control wire not reaching pin 7" looks identical to "ESP never asked".

**Next steps, in order:**
1. 12 V between hopper pins 1–3 and 4–6 (the lit coin LED proves a 12 V rail at
   the optocoupler, not at the hopper).
2. Power-cycle the ESP (it is in `error`), start a quantity-1 dispense, watch
   the **D1** optocoupler LED for the 5 s. Off → ESP side / D1 wiring / module.
   On → ESP is doing its part.
3. Voltage at hopper pin 7 while that LED is on: must be < 0.5 V. ≥ 1 V → the
   330 Ω parallel resistor on module #1 is missing (`hardware/README.md`).
   < 0.5 V and still no motor → hopper.

### C. Hypothesis that explains A and B at once: ribbon cable off by one

Hopper pins: 4–6 GND, 7 control, 8 error, 9 coin, 10 empty. Shifted one
position towards 10: control lands on 8 (error line → no motor), error lands on
9 (coin, high at rest → "inactive", as seen on D5), coin lands on 10 (empty
sensor; on a hopper that has it, LOW = *not empty* → LED on permanently). It
would also explain why D6 never produced anything. Cheap to check: count pins
at the connector against `error-signal-wiring.md` and `azkoyen-pinout.jpg`.

### D. Second header in the connector-6 window

Right of the 2×3 header there is a second header (2 rows, ~4–5 columns) with
what may be a reddish cap in the middle. Not described in our notes. If the
jumper that was pulled came from *there*, that needs the manual, not
connector 6.

## Pin 6 is *H Level*, not GND — sensor fitment still unknown (2026-09-24)

`docs/azkoyen-pinout.jpg` is the label on **our** unit. Its connector drawing
(key on the left, looking at the pins) reads:

```
 top row:    VCC | GND | H Level | Error   | Empty   ← pins 2 4 6 8 10
 bottom row: VCC | VCC | GND     | Control | Coin    ← pins 1 3 5 7 9
```

Column-wise numbering, odd pins bottom. That matches the manual's third table:
1/2/3 Vdc, **4/5 GND, 6 = full sensor ("H Level")**, 7 control, 8 error,
9 coin, **10 empty sensor**.

**What this proves and what it does not.** The manual says there is only one
board for the whole U-II parallel family, so the label most likely describes
what the pins are *designated* for, not what this unit has fitted. Certain:
**pin 6 is never GND** — either an open-collector output or nothing at all,
unusable as ground either way. Not certain: whether the full and empty sensors
exist. Evidence for: two cables in the screwed top cover, where §3.2.4 puts the
full-sensor photocell (board connector 5). Evidence against an empty sensor:
D6 never produced a signal — which may equally have been the wiring.

Settle it with 12 V on, black probe on pin 4:
- **pin 6**: high at rest; a hand/cardboard across the top of the bowl where
  the lid cables end → ≈ 0 V if the full sensor is fitted and working.
- **pin 10**: ≈ 0 V with tokens in the bowl, high when empty; stays high with
  tokens → no empty sensor (or dead), and dropping `hopper_low` was right.

**Measured unpowered at the hopper's pins (2026-09-24):** 4↔6 no continuity
(pin 6 is not GND on this unit — label confirmed); 1↔4 no continuity; 7↔4
≈ 7 kΩ (control input present; this is the lower leg of the divider with the
10 kΩ pull-up on opto #1 that gives the ≈ 6 V "motor OFF" level). Nothing
abnormal at the connector; the remaining questions — sensors fitted, exit
sensor free, motor — need 12 V on the hopper. Hopper-only test without the
ESP: 12 V on pin 1/4, read pin 9 vs 4 (high = sensor free), then bridge 7→4
briefly for the motor.

Also from the manual's figure 1: the second header in the configuration window
is board connector 7, **Tools connector**; a jumper there has no documented
function for us.

Photo `hopper-connector-2026-09-24.jpg`: the hopper's header has a triangle
mark on the left and a polarizing slot in the bottom wall, drawn the same way
on the label — so the label's drawing is the physical orientation.

Cable side (female, looking into the openings, its rib at the bottom) is the
mirror image: **rows stay, columns run backwards**:

```
 top:    Empty | Error   | H Level | GND | VCC     pins 10 8 6 4 2
 bottom: Coin  | Control | GND     | VCC | VCC     pins  9 7 5 3 1
```

Anchor by measurement: the three 12 V contacts are the right-hand corner
(1/2/3); left of pin 3 (bottom) is GND 5, left of pin 2 (top) is GND 4; left of
pin 4 (top, middle) is **pin 6 = H Level, not GND**. 4↔5 beep unpowered,
4↔6 must not.

## Hopper-only test (2026-09-24): motor runs

With 12 V on pins 1/4 and **pin 7 bridged to GND, the motor runs.** The hopper
is fine on the motor side, no latched error. "No motor" is therefore in our
control path: Wemos D1 → opto #1 → wire to pin 7. Causes 1 and 3 of the ranked
list are eliminated for the motor symptom; cause 4 is now the main suspect.

**Pin 9 vs GND at the breakout: ≈ 0 V at rest, ≈ 1.2 V while the motor runs
(no change per token), back to 0 V after.** Tokens do come out, so mechanics
and motor are free.

**This measurement is inconclusive — my instruction was incomplete.** Pin 9 is
an open collector; without a pull-up it reads ≈ 0 V in *both* states, because
the node floats when the transistor is off. In the real circuit the pull-up is
the opto's 12 V → 1 kΩ → LED path; at the breakout there is none. The 1.2 V
during the run is most likely ground shift: up to 2 A of motor current through
a thin Dupont GND lead lifts the hopper's ground by that much relative to the
probe. Use heavier wire for +12 V/GND at the breakout.

**Decisive version:** 4.7–10 kΩ from pin 9 to +12 V at the breakout, then:
≈ 12 V at rest / ≈ 0 V with a token held in the slot / a dip per token during a
run = sensor OK; ≈ 0 V throughout = sensor permanently occupied (Error 1/6);
≈ 12 V throughout = sensor blind (Error 2/6). Same applies to pins 6, 8, 10.
The permanently lit coin LED on the original cable remains an indication of
"occupied", but only if that wire really sits on pin 9.

If the pull-up test says "occupied", 12 V off: inspect and clean the exit slot and the two photocell windows;
check the red sensor module's seat and its plug on board connector 2 (reseat
if reachable); re-measure pin 9. Still 0 V → sensor module faulty. To tell 1
from 6: watch pin 8 right after 12 V on — an error string immediately at
power-on points to 6 (connector/photodiode), only after a motor run to 1.

Next: checklist step 8 with the ESP — D1 = 3.3 V during a dispense, opto #1 LED
on, hopper pin 7 < 0.5 V. Pin 7 stuck at ≈ 6 V with the LED on → opto #1 output
side / missing 330 Ω; stuck at 12 V → wire not reaching pin 7.

## Coin sensor works — polarity is the reverse of the manual (2026-09-24)

A spare PC817 module on the breakout (IN+ → +12 V, IN− → pin 9) as the
pull-up-with-indicator: **LED on at rest, off once per token during a motor
run, on again afterwards.** So on our unit the coin output is **conducting at
rest and releases for 30–65 ms per token** — the opposite of manual §3.2.2
("at rest high, coin pulls low") and of everything our docs copied from it.

Consequences:

- The permanently lit coin LED on the original cable was **normal**. The whole
  "sensor occupied / Error 1 / Error 6" line of reasoning is withdrawn.
- The firmware counts FALLING edges, which with this polarity is the **end** of
  the token pulse: one token, one edge, so counting worked before — 30–65 ms
  late. Tuesday's 0-pulse jam had no pulses because the motor never ran.
- **Everything reduces to one fault: the control path** Wemos D1 → opto #1 →
  wire to hopper pin 7. Next and only step: checklist step 8.

For the firmware ([#5](https://github.com/dgloeckner/remote-token-dispenser/issues/5)):
edge selection must be configurable (`RISING` on this hopper stops the motor at
the *start* of the pulse — the 30–65 ms the double-dispense fix `a9f15af` was
chasing); `coin_pulse.active` is inverted for this unit; the simulator in
[#1](https://github.com/dgloeckner/remote-token-dispenser/issues/1) must
reproduce the real polarity.

## Authoritative wiring for the rewire (2026-09-24)

Compiled from `config.h`, `critical-findings-2026-02-13.md` and today's
polarity measurement. `hardware/README.md` contradicts itself ("D1 LOW =
motor ON" is wrong) — do not wire from it. February's motor failure had three
causes, all in this path: opto #1 output-side VCC left open, R1 on #1 unmodified,
hopper in POSITIVE (jumper in the middle column — now removed).

**Opto #1, motor (D1 → pin 7)** — the only modified module

| Module terminal | Goes to | Note |
|---|---|---|
| IN+ | Wemos **D1** | GPIO HIGH = LED on = motor ON |
| IN− | Wemos **G** | |
| VCC (output side) | **+12 V** | February fault #1 — must not be open |
| OUT | Hopper **pin 7** | |
| GND (output side) | Hopper **GND, pin 4 or 5** | never pin 6 |
| R1 | **330 Ω in parallel** with the 1 kΩ | February fault #2 |
| R2 | 10 kΩ, **stock** | remove February's 1 kΩ parallel, per README |

**Opto #2 coin (pin 9 → D7) and #3 error (pin 8 → D5)** — stock resistors

| Module terminal | #2 coin | #3 error |
|---|---|---|
| IN+ | +12 V | +12 V |
| IN− | hopper **pin 9** | hopper **pin 8** |
| VCC (output side) | Wemos **3V3** | Wemos **3V3** |
| OUT | Wemos **D7** | Wemos **D5** |
| GND (output side) | Wemos **G** | Wemos **G** |

Module #4 stays on the breakout as the coin indicator; D6 free. Grounds are
isolated by the optos on purpose; a single Wemos-G ↔ hopper-GND bridge is
harmless and only matters for making measurements comparable.

**Before the first ESP dispense** (12 V on, Wemos on, motor off): hopper pin 7
vs hopper GND = 4–12 V (< 0.5 V would mean the motor is already running);
coin LED on #2 lit at rest — correct for this hopper. Then step 8; expected:
D1 3.3 V, LED #1 on, pin 7 < 0.5 V, motor runs, coin LED blinks off per token,
`dispensed` counts up.

## After the rewire (2026-09-24): dispensing and counting work; error line open

Motor runs from the ESP, tokens come out, `dispensed` counts correctly via the
TUI. The control-path fault is gone after re-wiring the Wemos to the three
optocouplers per the table above; the specific missing/wrong connection was
not identified.

Remaining: **the error line.** `/health` → `error_signal raw=0, active=true`;
the error LED on opto #3 is on and *pulsing*; `error_history` empty; metrics 0
(ESP rebooted after the dispense). Reading: the hopper repeats an error string
"until the next activation" (manual §3.3.2), and the decoder cannot see it
because this line, like the coin line, is most likely **conducting at rest**
with the pulses as releases — the decoder assumes the opposite (falling edge =
pulse start, measures the LOW phase), so it never leaves IDLE. A second gap:
an unrecognised string is stored as `ERROR_NONE`, which `getAll()` filters
out, so an unknown string is invisible in `/health`.

To do: 12 V off/on without a dispense — error LED steadily on at rest =
inverted polarity confirmed. Then a debug build with the decoder's edge
polarity flipped (`error_decoder.cpp handlePinChange`) to read the code.
Both polarity settings belong in `config.h`; the `ERROR_NONE` filtering
belongs in #6.

## Error line decoded by hand (2026-09-24): Error 1, repeating

Debug firmware `1.1.2-DEBUG-error-line` exposes raw edges and pulse widths in
`/health.error_line_debug`. Hopper powered alone, no dispense: **184 edges per
10 s ≈ 92 pulses/10 s, period ≈ 109 ms, every measured pulse 10 ms**, never a
100 ms one. So the line is ≈ 99 ms low + 10 ms high, endlessly. That is the
manufacturer's format for **one code pulse** — 100 ms start + 1 × 10 ms,
repeated until the next activation — i.e. **Error 1, "coin permanently in
exit sensor"**, sent from ~30 s after power-on without any motor run. The
decoder misses it because the start pulse lies on the rest level.

This favours a different reading of the coin LED than the one above: taken
literally from the manual (coin output pulls low when a token is seen), "LED on
at rest" means **the exit sensor reports a token all the time**, which is
exactly what the hopper says with Error 1. Per token the LED drops out briefly
(the passing token lights the receiver differently), and the ESP counts the
return-to-occupied edge — counting works by accident.

| | coin LED at rest | error string | after cleaning/reseating the sensor |
|---|---|---|---|
| A: exit sensor sees "occupied" permanently (dirty, misaligned, weak emitter) | on | Error 1 | LED off at rest, `edges` stops |
| B: this hopper's output is genuinely inverted | on | should not appear | unchanged |

Error 1 favours A. **Next:** 12 V off, clean the slot and both photocell
windows, reseat the sensor module, 12 V on, wait 40 s, read the coin LED and
`error_line_debug.edges`. If A, the polarity comment on #5 is to be withdrawn.

Decoder rework regardless: count 10 ms code pulses; a low phase > 50 ms ends
the string; no separate start-pulse detection. Error 1 would then have shown
in `/health` today.

## Correction (2026-09-25): the loose board was the PLUS sensor; the exit sensor was always plugged in

Hopper opened (`hopper-exit-sensor-2026-09-25.jpg`, `hopper-board-2026-09-25.jpg`).
The **coin-exit sensor is the black reflexive block moulded into the housing at
the exit path**, on its own small PCB, and it **is plugged into board
connector 2** (the red 2×3 header beside the white machine connector) — and
always was. The fork-shaped PCB found loose on 2026-09-24 is the second,
*double count* sensor of the U-II **Plus** kit (with the red LED on the
trigger's metal plate); in STANDARD mode (no PLUS jumper) it has no function,
so its missing cable is irrelevant. Yesterday's "sensor was unplugged"
conclusion is withdrawn; what stands:

- the manual's polarity is right (coin line pulled to 0 while a token is seen);
- the hopper reports "token permanently in exit sensor" (one-pulse error
  string in a loop) with the sensor connected;
- the per-token dropout that made counting "work" is the sensor briefly
  reading *differently* while a token passes — counting is accidental.

A reflexive sensor says "token" whenever its receiver sees light. Candidates,
cheapest first: **(1) ambient light** through the open exit slot (IR
photodiode; daylight/halogen read as reflection — shade the sensor and watch
the coin LED); **(2) a reflector in front of the lenses** — the trigger's
zinc plate sits right beside the block and moves; a token/fragment in the
path; **(3) dirty lenses** — the manufacturer recommends cleaning the exit
optics with cotton swabs. If none of these clears it, measure the sensor's
signal pin at connector 2 with a finger in front of the lenses; no change =
the sensor PCB itself is the spare part, not a cable.

**Sensor removed from the housing, held against black cloth, 12 V on:** error
string starts immediately, coin line held at "token". Caveat: black textiles
are often bright in IR, so repeat pointing the sensor into free space (≥ 20 cm
to anything, dim light). If unchanged, the sensor PCB is the fault: a
reflexive sensor whose *emitter* dies reports "never a token" (Error 2, line
free); one whose *receiver* leaks or whose lens couples emitter light
internally reports "always a token" (**Error 1**, line held) — which is what
we see, including the per-token disturbance that made counting look like it
worked. Decisive measurement: signal wire at connector 2 vs hopper GND, sensor
in free space vs a token 3 mm in front; same level both ways = sensor dead.
Spare part is the small sensor PCB (number on it; Eurocoin GmbH, hopper
41196311-0), or a donor hopper.

**Decision (2026-09-25):** a second U-II is bought as donor / spare. Buy a
*parallel* variant (label with the NEGATIVE/POSITIVE/PULSES matrix; ours is
41196311-0), tested if possible, and check the coin disc fits our tokens. On
arrival: connector-6 header without caps, then the standalone sensor test
(IR LED via 150 Ω at 3.3 V, phototransistor with 10 kΩ pull-up; free space ≈
3.3 V, token 3 mm ≈ 0 V, emitter off + token ≈ 3.3 V) before it goes on the
cable. Firmware work #1–#4 does not wait for it; #5 and Cycle B do.

Firmware state unchanged: `1.1.3-DEBUG-error-line` flashed, polarity default
0, `error_line_debug` in `/health`.

## Resolution (2026-10-01): both hoppers are *Plus* hardware run in STANDARD mode

Final picture after the cable was verified contact by contact (coin on pin 9,
error on pin 8, control on pin 7), ambient light, supply (never below 12 V
under motor start), tokens (hopper fully emptied) and a second unit were all
excluded:

| Mode (connector 6) | At rest | Dispense | Error channel |
|---|---|---|---|
| STANDARD (no cap) | **Error 1 string in a loop** (100 ms low + one 10 ms pulse, ≈ 9/s), coin line held low — exactly the manual's consequence of Error 1 | runs, counts exactly (2, 3, 10, 14) | useless, always Error 1 |
| PLUS + NEGATIVE (cap on the right column) | clean: both lines high, zero edges | **motor refuses to run** (0/2, jam timeout, no string) | usable once the Plus sensor is connected |

Both units carry the U-II **Plus** kit (fork-shaped double-count sensor at
the trigger, red LED on the trigger plate) and on **both the sensor's cable is
missing** — presumably removed when the hoppers left their gaming machines
and were set to STANDARD. On this hardware STANDARD leaves the un-read Plus
trigger in the exit sensor's view → "coin permanently in exit sensor". The
manual's polarity is **correct**; every earlier "inverted output" reading in
this file is withdrawn. Counting in STANDARD works because the hopper still
releases pin 9 once per token that passes the sensor.

**Decision:** A — run in PLUS mode with the Plus sensor connected (signals per
spec, error channel meaningful). Needs one 3-wire cable per hopper, fork
board ↔ board connector 3 ("Double count"), pinout to be measured on the open
unit. B — STANDARD as the interim: dispenses exactly today; firmware must
treat "Error 1 at rest" as baseline and keep the 5 s timeout as the only jam
detection.

Firmware consequences (→ #5, #6): coin edge stays FALLING (start of the
30–65 ms low pulse, per spec — the earlier RISING advice applied to the
STANDARD artefact and is withdrawn); the decoder must handle a one-pulse
string repeated back-to-back (today it discards it) and must never hide an
unknown string; `error_line_debug` in `/health` stays until the decoder is
proven against Error 6 in PLUS mode.

## Documentation errors found

- `docs/azkoyen-hopper-protocol.md` §3.2.2 and `hardware/README.md` ("coin pulse … active LOW"): on our unit the coin line is LOW at rest and HIGH per token. Re-measure before restating either.

- `hardware/README.md` §"Connector Pins": "GND (4, 6)" and "H Level (5)" are
  swapped — on our unit 4/5 are GND and **6 is H Level**. Every wiring note
  that says "pins 4, 5, 6 GND" inherits the error.

- `docs/azkoyen-hopper-protocol.md` §5 and §8.1 describe connector 6 as a
  **DIP switch** with "● = ON (closed)" and "all six ON" for STANDARD+NEGATIVE.
  The manufacturer figure shows a **jumper header** where ● is an open pin and
  a bar is a cap; STANDARD+NEGATIVE = **no caps**. The table in §5 also gives
  NEGATIVE and PULSES the same pattern. Rewrite from figure 5.
- TUI `views.go:301-305`: coin-pulse label is inverted (`active=true` printed
  as "idle (default)"; firmware defines `active` = pin LOW = coin in sensor).
  "Hopper Empty: EMPTY" row reads the pull-up of an unconnected D6 as a sensor
  result. Both mislead at the bench.
- `firmware/README.md`: monitor baud 115200 → 9600 (or fix `Serial.begin`).
- `docs/TESTING.md` still names `192.168.188.244`.

## Behaviour observed that the epic already covers

- POST `/dispense` latency 616 ms → [#4](https://github.com/dgloeckner/remote-token-dispenser/issues/4).
- Device parks in `error` after every failed attempt and needs a power cycle;
  it still accepts new POSTs in that state → [#6](https://github.com/dgloeckner/remote-token-dispenser/issues/6).
- No way to run the motor briefly without a transaction, a jam, a metrics hit
  and a power cycle. A bounded, authenticated `POST /debug/motor {"ms":300}`
  behind a `DIAGNOSTICS` build flag, returning the pulses seen, would have
  replaced three power cycles today → proposed for #6 (firmware) and #1 (TUI).
- A probe `POST /dispense quantity:1` used as an auth check started a real
  dispense. Use an invalid body (expects 400) to test a key, never a valid one.

## Metrics at end of session

`total_dispenses 1, jams 1, dispensed_tokens 0`, `dispenser=error`, coin line
still low with the cable in.

## Ranked causes (2026-09-23) — assumes the circuit worked before and the PSU is fine

The manufacturer's table 1 (`hopper-protocol.pdf` p. 8), **Error 1** *"coin
permanently in exit sensor"* → consequence: **"Stop the Hopper and coin signal
permanently ON"**. That is both symptoms verbatim. The error pulse string on
pin 8 is only sent *until a new activation order arrives*, which fits the empty
`error_history`. And so far only the ESP was power-cycled — **never the
hopper**, so a latched hopper error survived every attempt.

| # | Cause | P | Note |
|---|---|---|---|
| 1 | Hopper latched in Error 1: exit sensor blocked (token, dust) or sensor faulty after storage | ~60 % | one cause for both symptoms, no wiring involved |
| 2 | Ribbon-cable/connector position or a single displaced wire | ~15 % | the plain off-by-one has a hole: the 12 V wires would land on GND pins and the fuse would have blown |
| 3 | The pulled jumper was on the *second* header in the window | ~10 % | function unknown; manual figure 1 shows the board's connectors |
| 4 | Control path (opto #1, 330 Ω mod, wire to pin 7) | ~10 % | explains the motor only |
| 5 | Other | ~5 % | |

**Order by yield:** (1) 12 V off, inspect and clean the exit chute/photocell;
(2) 12 V on, watch opto #2 and #3 LEDs in the first seconds — coin LED off =
fixed; error LED flickers = hopper reports at self-test, read the code in
`/health.error_history` (1 blocked, 2 stuck free, 6 sensor disconnected);
(3) continuity mapping, step 9 below; (4) only then step 8. Steps 0–2 and 6–7
are skipped under these assumptions.

## Multimeter checklist (2026-09-23) — no cable is opened

All voltages DC, black probe on the **hopper GND** (pin 4/5/6 at the ribbon
connector) unless stated. Back-probe at screw terminals and connector pins;
nothing is unplugged except the USB for a power cycle.

Three phases, in this order:

| Phase | Power state | Meter | Steps |
|---|---|---|---|
| A | 12 V PSU **off**, USB **off** | continuity | 0, 9 — only valid unpowered: under voltage a beeper also conducts through semiconductors and pull-ups |
| B | 12 V **on**, USB **on**, ESP booted (`/health` answers) | DC V, 20 V range | 1–7, rest measurements; ESP may sit in `error`, irrelevant here; nothing is plugged or unplugged |
| C | 12 V stays **on**; USB off/on once to get the ESP from `error` to `idle` (~20 s until WiFi is back) | DC V | 8 — probe already on `D1`, then TUI qty 1 + Enter; move to hopper pin 7 within the 5 s window. If time runs out: USB off/on, trigger again, measure pin 7 directly. Every attempt ends in `error` → USB off/on before the next |

| # | Test | Red probe | Expect | If not |
|---|---|---|---|---|
| 0 | Common ground (power off, continuity) | ESP `G` pin ↔ hopper GND | beep, < 1 Ω | no common ground: nothing below is meaningful |
| 1 | Hopper supply | hopper pin 1/2/3 | 11.5–12.5 V | PSU, fuse F1, cable |
| 2 | ESP supply | ESP `3V3` (black on ESP `G`) | 3.2–3.4 V | buck converter / USB |
| 3 | Coin line at rest | hopper pin 9 | ≈ 12 V (open collector, no current) | ≈ 0 V: hopper holds it low → token in sensor, or wrong pin (test 9) |
| 4 | Coin at ESP, rest | opto #2 OUT / ESP `D7` (black on ESP `G`) | 3.3 V | 0 V while pin 9 is 12 V: opto #2 output or D7 wiring |
| 5 | Coin, sensor blocked by a token | pin 9, then D7 | 0 V, then 0 V | pin 9 stays 12 V: exit sensor not seeing the token, or wrong pin |
| 6 | Error line at rest | hopper pin 8, then ESP `D5` | ≈ 12 V, 3.3 V | — |
| 7 | Motor line at rest | hopper pin 7 | 4–12 V (≈ 6 V typical, motor OFF) | < 0.5 V: motor would be running — wiring/opto #1 |
| 8 | Motor line during a qty-1 dispense (5 s window, ESP power-cycled first) | ESP `D1` (black on ESP `G`), then hopper pin 7 | 3.3 V, then < 0.5 V | D1 stays 0 V: firmware/ESP. D1 3.3 V but pin 7 ≥ 1 V: opto #1 (330 Ω mod, R1) or wire to pin 7. Pin 7 < 0.5 V and no motor: hopper |
| 9 | Ribbon-cable mapping (power off, continuity) | hopper pin 7 ↔ opto #1 output terminal; pin 9 ↔ opto #2 IN−; pin 8 ↔ opto #3 IN−; pin 4/5/6 ↔ opto IN+ side supply return | beep on the expected pair only | beep on the neighbouring pin: cable off by one (hypothesis C) |
