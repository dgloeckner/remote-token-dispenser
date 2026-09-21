# 🪙 Token Dispenser TUI

A k9s-style terminal dashboard for testing and monitoring the [Remote Token Dispenser](../README.md) HTTP API.

```
 🪙 Token Dispenser TUI           http://192.168.4.20  ● connected
 1:Dashboard │ 2:Dispense │ 3:Log │ 4:Burst Test
──────────────────────────────────────────────────────────
╭─────────────────────────╮  ╭────────────────────────────╮
│ ⚡ Health               │  │ 📊 Metrics                 │
│                         │  │                            │
│ State:      ● idle      │  │ Total Dispenses: 1247      │
│ Fault:      none        │  │ Success Rate:    95.4%     │
│ Uptime:     23h 27m     │  │ Jams:            3         │
│ Firmware:   1.3.0       │  │ Partial:         2         │
│ Heap:       27512 B     │  │ Failures:        53        │
│ Last reset: Power on    │  │                            │
│ WiFi:       ▂▄▆ -47dBm  2 reconnects                    │
╰─────────────────────────╯  ╰────────────────────────────╯
╭─────────────────────────────────────────────────────────╮
│ 📈 Latency (ms)                                        │
│   ▂▃▂▁▃▂▁▁▂▃▄▃▂▁▂▃▂▁▁▂▃▂▁▁▂▃▅▃▂▁▂▃▂▁               │
│   min:12ms  avg:23ms  max:45ms  samples:36             │
╰─────────────────────────────────────────────────────────╯
──────────────────────────────────────────────────────────
 ↑↓ qty │ ⏎ dispense │ 1-4 tabs │ r refresh │ q quit
```

## Install & Run

```bash
# Clone and build
cd token-tui
go mod tidy
go build -o token-tui .

# Run
./token-tui --endpoint http://192.168.4.20 --signing-key your-secret-key

# Or use env vars
export TOKEN_DISPENSER_SIGNING_KEY=your-secret-key
export TOKEN_DISPENSER_ENDPOINT=http://192.168.4.20
./token-tui
```

## Features

### 1. Dashboard (Tab 1)
- Real-time health monitoring with auto-refresh every 5s
- Device `state` and `fault` (issue #6), uptime, firmware version
- **WiFi signal strength with visual bars** (NEW)
- Dispense metrics: success rate, jams, partial dispenses, failures
- Latency sparkline with min/avg/max stats
- **GPIO debug overlay** - toggle with `D` key (NEW)
- Recent request log

### 2. Dispense (Tab 2)
- Interactive quantity selector (1-20 tokens)
- Visual coin indicator
- Live progress bar during dispensing with coin drop animation
- TX ID tracking, elapsed time, success/error feedback

### 3. Test Cycle (Tab 3) - UPDATED
- **Preset test quantities**: Single (1), Typical (3), Stress (10), Custom (1-20)
- Live progress bar during test with coin drop animation
- TX ID tracking, elapsed time, success/error feedback
- Last test result display with timing
- Quick health refresh with `H` key

### 4. Request Log (Tab 4)
- Full request history with timestamps, methods, status codes, latency
- Scrollable with keyboard navigation
- Color-coded status: green=2xx, yellow=4xx, red=5xx/errors

## Keyboard Shortcuts

| Key     | Action                           |
|---------|----------------------------------|
| `1-4`   | Switch tabs                      |
| `r`     | Force health refresh             |
| `d/D`   | Toggle GPIO debug overlay (NEW)  |
| `q`     | Quit                             |
| `↑/↓`   | Adjust quantity / scroll         |
| `Enter` | Start dispense / test            |
| `g/G`   | Jump to top/bottom of log        |
| `C`     | Clear result / log               |
| `H`     | Force health refresh (Test tab)  |

## Conformance mode (headless)

The same binary also runs the protocol conformance suite — the table of cases
that decides whether an implementation speaks `dispenser-protocol.md`:

```bash
# against the Go mock (this is what CI runs)
token-tui conformance --endpoint http://127.0.0.1:8080 --signing-key dev --target mock

# against a real ESP8266 with the hopper simulator
token-tui conformance --endpoint http://192.168.4.20 --signing-key mysecret \
  --target simulator --interactive --json report.json
```

| Flag | Meaning |
|------|---------|
| `--target` | `mock`, `simulator` or `hopper` — decides which cases apply |
| `--interactive` | also run cases that ask for a physical act (press RST, flip a switch) |
| `--json PATH` | write a per-case report |
| `--only SUBSTR` | run only the cases whose name contains SUBSTR |
| `--soak N` | run a soak of N dispenses **instead of** the table (issue #7) |
| `--soak-poll D` | how often a soak polls the running transaction (default 500ms) |

A soak is the other half of the verdict: the table says the device speaks the
protocol, the soak says it still does after two hundred dispenses — zero failed
requests, free heap within 10 % of where it started, no reset in the middle,
and a p95 POST latency under 300 ms (which is where modem sleep shows up and
nowhere else).

```bash
# Cycle A of the epic, with the hopper simulator in fast mode ('f')
token-tui conformance --endpoint http://192.168.4.20 --signing-key mysecret \
  --target simulator --soak 200 --json report-A.json
```

The exit code is the verdict. Cases are defined in `conformance_cases.go`;
`conformance.go` is the runner. Both are covered by `go test` against a fake
device, so a case that can never fail is caught.

## Dependencies

- [Bubble Tea](https://github.com/charmbracelet/bubbletea) — TUI framework
- [Lip Gloss](https://github.com/charmbracelet/lipgloss) — styling
- [Bubbles](https://github.com/charmbracelet/bubbles) — components
- [google/uuid](https://github.com/google/uuid) — TX ID generation
