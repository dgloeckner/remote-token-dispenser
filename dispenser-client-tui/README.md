# 🪙 Token Dispenser TUI

A k9s-style terminal dashboard for testing and monitoring the [Remote Token Dispenser](../README.md) HTTP API.

```
 🪙 Token Dispenser TUI           http://192.168.4.20  ● connected
 1:Dashboard │ 2:Dispense │ 3:Log │ 4:Burst Test
──────────────────────────────────────────────────────────
╭─────────────────────────╮  ╭────────────────────────────╮
│ ⚡ Health               │  │ 📊 Metrics                 │
│                         │  │                            │
│ Status:     ● OK        │  │ Total Dispenses: 1247      │
│ Dispenser:  idle        │  │ Success Rate:    95.4%     │
│ Uptime:     23h 27m     │  │ Jams:            3         │
│ Firmware:   1.2.0       │  │ Partial:         2         │
│ Hopper:     ● OK        │  │ Failures:        53        │
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
./token-tui --endpoint http://192.168.4.20 --api-key your-secret-key

# Or use env vars
export TOKEN_DISPENSER_API_KEY=your-secret-key
export TOKEN_DISPENSER_ENDPOINT=http://192.168.4.20
./token-tui
```

## Features

### 1. Dashboard (Tab 1)
- Real-time health monitoring with auto-refresh every 5s
- ESP8266 status, uptime, firmware version, hopper status
- Dispense metrics: success rate, jams, partial dispenses, failures
- Latency sparkline with min/avg/max stats
- Recent request log

### 2. Dispense (Tab 2)
- Interactive quantity selector (1-20 tokens)
- Visual coin indicator
- Live progress bar during dispensing with coin drop animation
- TX ID tracking, elapsed time, success/error feedback

### 3. Request Log (Tab 3)
- Full request history with timestamps, methods, status codes, latency
- Scrollable with keyboard navigation
- Color-coded status: green=2xx, yellow=4xx, red=5xx/errors

### 4. Burst Test (Tab 4)
- Sequential stress testing (configurable count + tokens per request)
- Progress tracking with success/failure counts
- Great for testing jam detection, recovery flows, and reliability

## Keyboard Shortcuts

| Key     | Action                           |
|---------|----------------------------------|
| `1-4`   | Switch tabs                      |
| `r`     | Force health refresh             |
| `q`     | Quit                             |
| `↑/↓`   | Adjust quantity / scroll         |
| `←/→`   | Adjust burst tokens per request  |
| `Enter` | Start dispense / burst           |
| `g/G`   | Jump to top/bottom of log        |
| `C`     | Clear request log                |

## Dependencies

- [Bubble Tea](https://github.com/charmbracelet/bubbletea) — TUI framework
- [Lip Gloss](https://github.com/charmbracelet/lipgloss) — styling
- [Bubbles](https://github.com/charmbracelet/bubbles) — components
- [google/uuid](https://github.com/google/uuid) — TX ID generation
