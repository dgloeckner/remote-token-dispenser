# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a remote token/coin dispenser system - a point-of-sale terminal for managing token or coin purchases and dispensing physical tokens (e.g., 2 EUR coins). Can be used for various applications such as saunas, washing machines, or any token-operated service. The system consists of:

1. **Raspberry Pi 5** - POS terminal running Flutter UI and Rust daemon
2. **ESP8266** (tested with Wemos D1) - Dispenser controller with WiFi HTTP API
3. **Azkoyen Hopper U-II** - Physical coin/token dispenser hardware

## System Architecture

The repository contains design specifications for a three-component architecture:

```
┌─────────────────────┐          ┌──────────────────┐          ┌─────────────────┐
│  Flutter Frontend   │  WiFi    │  ESP8266           │  GPIO    │ Azkoyen Hopper  │
│  (Raspberry Pi 5)   │  HTTP    │  (Wemos D1)      │─────────▶│ (Hardware)      │
│  - User UI          │─────────▶│  - HTTP Server   │          │ - Motor control │
│  - RFID reader      │          │  - State machine │          │ - Token counter │
│  - Transaction DB   │          │  - Crash recovery│          │ - Opto sensor   │
└─────────┬───────────┘          └──────────────────┘          └─────────────────┘
          │
          │ supervises
          ▼
┌─────────────────────┐
│  POS Daemon (Rust)  │
│  - Health monitor   │
│  - Watchdog         │
└─────────────────────┘
```

## Key Design Documents

### ARCHITECTURE.md
Comprehensive system architecture with mermaid diagrams:
- **Component architecture** - Pi, ESP8266, and Hopper interaction
- **Transaction flows** - Two-phase, single-phase, cancellation, conflicts
- **State machines** - ESP8266 and Pi transaction lifecycles
- **Error recovery** - Crash scenarios, network timeouts, hardware jams
- **Sequence diagrams** - Detailed flow visualization
- **Deployment** - Physical layout, network topology, data persistence

### azkoyen-hopper-protocol.md ⚠️ CRITICAL REFERENCE
**Official Azkoyen Hopper U-II parallel protocol specification** extracted from manufacturer documentation:
- **MUST READ** before modifying hopper control code
- **Connector pinout** - 10-pin Molex, power/signal assignments
- **Working modes** - NEGATIVE (required!), POSITIVE, PULSES
- **Control signal (Pin 7)** - Voltage thresholds, timing requirements
- **Coin detection (Pin 9)** - Pulse characteristics (30-65ms per coin)
- **Error codes (Pin 8)** - 7 error types, pulse-encoded reporting
- **DIP switch configuration** - STANDARD + NEGATIVE mode required
- **Timing specifications** - Control pulse widths, pulse validation
- **Electrical specs** - Voltage levels, open collector outputs

**Location:** `docs/azkoyen-hopper-protocol.md`
**Source:** Official Azkoyen manual (`docs/hopper-protocol.pdf`)

**Why critical:** Violating protocol specs (wrong DIP switch mode, incorrect voltage levels) causes complete motor control failure. Always verify against this document when debugging hopper issues.

**Troubleshooting:** See `docs/troubleshooting/motor-control-issues.md` for step-by-step diagnosis of common hardware issues (NEGATIVE mode, optocoupler saturation, voltage levels).

### dispenser-protocol.md
Defines the WiFi HTTP protocol between Raspberry Pi and ESP8266:
- **Idempotent transactions** with client-generated `tx_id`
- **State machine**: `idle` → `reserved` → `dispensing` → `done`
- **Endpoints**:
  - `GET /health` - connectivity and status
  - `POST /dispense` - reserve/confirm/cancel actions
  - `GET /dispense/{tx_id}` - poll transaction status
- **Error recovery** for crashes, network timeouts, and hardware jams
- Local SQLite on Pi is source of truth; backend sync is eventual

### pos-daemon-design.md
Rust system daemon on Raspberry Pi:
- **ESP8266 health polling** - monitor dispenser status every 60s
- **healthchecks.io reporting** - aggregate system health
- **Frontend watchdog** - restart Flutter app if unresponsive
- **System monitoring** - disk, temp, sync queue metrics
- Runs as systemd service with sd_notify integration

### enclosure-design.md
Physical hardware design:
- **Display unit**: KKSB Display Stand for Pi 5 + Touch Display 2
- **Dispenser unit**: IP65 metal junction box (~200×150×100mm)
- Component layout, cable routing, token exit chute
- Electrical safety and WiFi considerations

## Transaction Model

The Pi maintains the transaction source of truth in local SQLite:
1. Pi writes transaction record BEFORE telling ESP8266 to dispense
2. ESP8266 dispenses tokens and tracks progress via `dispensed` count
3. Pi polls status and updates local record with actual outcome

Transaction fields: `tx_id`, `user_id`, `quantity`, `dispensed`, `state`, `timestamp`

## Crash Safety

**ESP8266 persistence**: Writes `{tx_id, quantity, dispensed}` to flash on state transitions. On reboot, recovers partial dispense state.

**Pi recovery**: On reboot, queries local DB for incomplete transactions, polls ESP8266 for current state, reconciles and resumes or completes.

**Network resilience**: All mutating requests are idempotent by `tx_id` - safe to retry without double-dispensing.

## Hardware Interfaces

### ESP8266 ↔ Azkoyen Hopper

⚠️ **CRITICAL:** See `docs/azkoyen-hopper-protocol.md` for complete Azkoyen protocol specification.

**Power:**
- 12V/2A DC adapter with 2200µF capacitor for motor startup surge

**Isolation:**
- 4× PC817 optocoupler modules (bestep brand) for galvanic isolation

**Control Logic:**
- **Hopper mode:** NEGATIVE (active LOW control - **mandatory DIP switch setting**)
- **Optocoupler wiring:** D1 → IN+, GND → IN- (not inverted)
- **Control signal chain:**
  - GPIO HIGH → Optocoupler LED ON → OUT LOW (< 0.5V) → Motor ON
  - GPIO LOW → Optocoupler LED OFF → OUT HIGH (~6V) → Motor OFF
- **Input signals:** All active LOW (coin pulse/error/empty detection)

**Pin Assignments:**
- **D1 (GPIO5)** → Control output (via PC817 #1)
  - ⚠️ Requires R1 modification: 330Ω in parallel with stock 1kΩ
  - Provides 13.3mA drive current for PC817 saturation
- **D7 (GPIO13)** ← Coin pulse input (via PC817 #2)
  - Active LOW, FALLING edge interrupt
  - 30-65ms pulses per coin (PULSES coin mode)
- **D5 (GPIO14)** ← Error signal input (via PC817 #3)
  - Active LOW (LOW = error condition)
- **D6 (GPIO12)** ← Empty sensor input (via PC817 #4)
  - Active LOW (LOW = NOT empty, HIGH = empty)

**Hopper Connector Pinout (10-pin Molex):**
- Pins 1, 2, 3: 12V VCC
- Pins 4, 5, 6: GND
- Pin 7: Control (motor activation)
- Pin 8: Error signal output
- Pin 9: Coin pulse output
- Pin 10: Empty sensor (optional)

**Critical Requirements:**
- Hopper DIP switch MUST be set to NEGATIVE mode (see protocol doc section 2.1)
- PC817 R1 must be modified (330Ω parallel) for reliable operation
- VCC pin on optocoupler modules MUST be connected to 12V

### Raspberry Pi ↔ ESP8266
- **WiFi HTTP**: Pi communicates with ESP8266 over local WiFi network
- **Display**: Official Touch Display 2 via DSI

## Development Notes

### Technology Stack
- **Frontend**: Flutter (not yet implemented)
- **Daemon**: Rust with `reqwest`, `rusqlite`, `systemd` crates
- **ESP8266**: C++ with Arduino framework (compatible with any ESP8266, tested with Wemos D1)
- **Database**: SQLite for local transaction storage

### Configuration
Daemon config at `/etc/pos-daemon/config.toml`:
- ESP8266 IP and polling intervals
- healthchecks.io URL
- Watchdog parameters

### Timing Constraints
- **ESP8266 health check**: 60s
- **Transaction reservation TTL**: 30s
- **Per-token dispense timeout**: 5s
- **Full dispense timeout**: 60s

### State Machine
ESP8266 tracks exactly one active transaction. States:
- `idle` - ready for new transaction
- `reserved` - locked, awaiting confirm
- `dispensing` - motor running, tokens dropping
- `done` - completed successfully
- `error` - hardware fault or jam
- `cancelled` - reservation released without dispensing

### Error Handling
- Hopper jam: partial dispense recorded with exact `dispensed` count
- Network timeout: idempotent retry safe
- Power loss: flash persistence enables recovery
