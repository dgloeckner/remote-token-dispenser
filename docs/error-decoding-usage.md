# Error Decoding Usage Guide

## Overview

The hopper error decoding feature decodes Azkoyen Hopper U-II error signals and maintains a history of the last 5 errors.

## Error Codes

| Code | Type | Description | Severity |
|------|------|-------------|----------|
| 0 | UNKNOWN | Malformed error signal | Low |
| 1 | COIN_STUCK | Coin stuck in exit sensor (>65ms) | Medium |
| 2 | SENSOR_OFF | Exit sensor stuck OFF | Medium |
| 3 | JAM_PERMANENT | Permanent jam detected | High |
| 4 | MAX_SPAN | Multiple spans exceeded max time | High |
| 5 | MOTOR_FAULT | Motor doesn't start | High |
| 6 | SENSOR_FAULT | Exit sensor disconnected/faulty | High |
| 7 | POWER_FAULT | Power supply out of range | High |

## API Usage

### GET /health

Returns error information:

```json
{
  "error": {
    "active": true,
    "code": 3,
    "type": "JAM_PERMANENT",
    "timestamp": 123456,
    "description": "Permanent jam detected"
  },
  "error_history": [
    {
      "code": 3,
      "type": "JAM_PERMANENT",
      "timestamp": 123456,
      "cleared": false
    }
  ]
}
```

## Self-Healing

Active errors are automatically cleared when a successful dispense completes. The error remains in history with `cleared: true`.

## TUI Display

- **Health Panel**: Shows active error type with color coding (yellow=sensor, red=critical)
- **Error History Panel**: Shows last 5 errors with age and cleared status

## Serial Monitor

Error events are logged:

```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
[ErrorHistory] Added error: JAM_PERMANENT at timestamp 12345
[ErrorHistory] Cleared active error: JAM_PERMANENT
```

## Testing

Simulate errors by creating pulse sequences on D5 (GPIO14):
1. 100ms LOW (start pulse)
2. N × 10ms LOW (code pulses, N = 1-7)
3. 200ms timeout

## Troubleshooting

- **No errors detected**: Check ERROR_SIGNAL_PIN wiring (D5 to hopper pin 8 via optocoupler)
- **Wrong error codes**: Verify pulse timing (100ms start, 10ms codes)
- **Errors don't clear**: Check dispense completion logic in DispenseManager
