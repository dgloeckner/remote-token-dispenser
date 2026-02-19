# Token Dispenser Mock

Mock HTTP server implementing the ESP8266 dispenser protocol for local development.

## Quick Start

```bash
# Start with defaults (localhost:8080, api-key=dev)
go run .

# Or build and run
go build
./dispenser-mock

# Custom configuration
./dispenser-mock --bind=:9090 --api-key=secret123

# List available test scenarios
./dispenser-mock --list-scenarios
```

## Configuration

**CLI Flags:**
- `--bind` - Network address (default: `:8080`)
- `--api-key` - Required API key (default: `dev`)
- `--list-scenarios` - Print scenario mapping and exit

## Test Scenarios

Request quantity determines test behavior:

| Quantity | Scenario |
|----------|----------|
| 1-3      | Normal success (100ms/token) |
| 4        | Timeout after 2 tokens |
| 5        | Crash after 1 token (connection closed) |
| 6        | Partial dispense (4 of 6) |
| 7        | Load delay (2.5s first token) |
| 8-14     | Hardware errors (COIN_STUCK, JAM, MOTOR_FAULT, etc.) |
| 15       | Slow dispense (500ms/token) |
| 16-20    | Normal success |

## Usage Examples

**Health check (no auth):**
```bash
curl http://localhost:8080/health | jq
```

**Start dispense:**
```bash
curl -X POST http://localhost:8080/dispense \
  -H "X-API-Key: dev" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"abc123","quantity":3}'
```

**Poll status:**
```bash
curl -H "X-API-Key: dev" http://localhost:8080/dispense/abc123
```

**Test with TUI client:**
```bash
cd ../dispenser-client-tui
go run . --url=http://localhost:8080 --api-key=dev
```

## Protocol

Implements [Dispenser Protocol v1.1.0](../dispenser-protocol.md):
- `GET /health` (public)
- `POST /dispense` (auth required)
- `GET /dispense/{tx_id}` (auth required)

## Implementation Notes

- **Thread-safe** - safe for concurrent requests
- **In-memory only** - no persistence, clean slate on restart
- **Realistic timing** - 100ms/token matches real hardware
- **Full idempotency** - transaction history ring buffer (8 entries)
- **No external dependencies** - stdlib only

## Design Document

See [Mock Dispenser Design](../docs/plans/2026-02-16-mock-dispenser-design.md) for complete architecture and scenario details.
