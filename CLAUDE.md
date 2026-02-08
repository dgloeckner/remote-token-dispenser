# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a remote token/coin dispenser system - a point-of-sale terminal for managing token or coin purchases and dispensing physical tokens (e.g., 2 EUR coins). Can be used for various applications such as saunas, washing machines, or any token-operated service. The system consists of:

1. **Raspberry Pi 5** - POS terminal running Flutter UI and Rust daemon
2. **ESP32** (tested with Wemos D1) - Dispenser controller with WiFi HTTP API
3. **Azkoyen Hopper U-II** - Physical coin/token dispenser hardware

## System Architecture

The repository contains design specifications for a three-component architecture:

```
┌─────────────────────┐          ┌──────────────────┐          ┌─────────────────┐
│  Flutter Frontend   │  WiFi    │  ESP32           │  GPIO    │ Azkoyen Hopper  │
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
- **Component architecture** - Pi, ESP32, and Hopper interaction
- **Transaction flows** - Two-phase, single-phase, cancellation, conflicts
- **State machines** - ESP32 and Pi transaction lifecycles
- **Error recovery** - Crash scenarios, network timeouts, hardware jams
- **Sequence diagrams** - Detailed flow visualization
- **Deployment** - Physical layout, network topology, data persistence

### dispenser-protocol.md
Defines the WiFi HTTP protocol between Raspberry Pi and ESP32:
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
- **ESP32 health polling** - monitor dispenser status every 60s
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
1. Pi writes transaction record BEFORE telling ESP32 to dispense
2. ESP32 dispenses tokens and tracks progress via `dispensed` count
3. Pi polls status and updates local record with actual outcome

Transaction fields: `tx_id`, `user_id`, `quantity`, `dispensed`, `state`, `timestamp`

## Crash Safety

**ESP32 persistence**: Writes `{tx_id, quantity, dispensed}` to flash on state transitions. On reboot, recovers partial dispense state.

**Pi recovery**: On reboot, queries local DB for incomplete transactions, polls ESP32 for current state, reconciles and resumes or completes.

**Network resilience**: All mutating requests are idempotent by `tx_id` - safe to retry without double-dispensing.

## Hardware Interfaces

### ESP32 ↔ Azkoyen Hopper
- 12V power (2A DC adapter)
- 2200µF capacitor for motor startup surge
- GPIO control pin for motor trigger
- Opto-sensor interrupt for token counting

### Raspberry Pi ↔ ESP32
- **WiFi HTTP**: Pi communicates with ESP32 over local WiFi network
- **Display**: Official Touch Display 2 via DSI

## Development Notes

### Technology Stack
- **Frontend**: Flutter (not yet implemented)
- **Daemon**: Rust with `reqwest`, `rusqlite`, `systemd` crates
- **ESP32**: C++ with Arduino framework (compatible with any ESP32, tested with Wemos D1)
- **Database**: SQLite for local transaction storage

### Configuration
Daemon config at `/etc/pos-daemon/config.toml`:
- ESP32 IP and polling intervals
- healthchecks.io URL
- Watchdog parameters

### Timing Constraints
- **ESP32 health check**: 60s
- **Transaction reservation TTL**: 30s
- **Per-token dispense timeout**: 5s
- **Full dispense timeout**: 60s

### State Machine
ESP32 tracks exactly one active transaction. States:
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
