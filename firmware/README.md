# ESP8266 Token Dispenser Firmware

HTTP-controlled token dispenser firmware for Wemos D1 Mini (ESP8266) controlling an Azkoyen Hopper U-II.

---

## Hardware Requirements

- **Board:** Wemos D1 Mini (ESP8266) - LOLIN(WEMOS) D1 R2 & mini
- **Dispenser:** Azkoyen Hopper U-II (configured in PULSES mode)
- **Power:**
  - ESP8266: 5V via USB or VIN pin
  - Azkoyen Hopper: 12V/2A separate power supply
- **Isolation:** 4× PC817 optocoupler modules (bestep brand with onboard resistors)
- **Resistor modification required** - See Critical Hardware Configuration below

---

## ⚠️ Critical Hardware Configuration

**Before flashing firmware, verify hardware is configured correctly:**

### 1. Hopper DIP Switch: NEGATIVE Mode

The Azkoyen Hopper U-II **MUST** be in NEGATIVE mode:
- Control signal: active LOW (< 0.5V = motor ON, > 4V = motor OFF)
- DIP switch: STANDARD + NEGATIVE configuration
- If in POSITIVE mode: motor behavior will be inverted
- **Protocol reference:** `docs/azkoyen-hopper-protocol.md` section 2.1
- **Setup guide:** [hardware/README.md](../hardware/README.md#-critical-configuration-requirements)

### 2. Optocoupler Resistor Modification

PC817 module #1 (motor control, channel D1) requires resistor modification:
- Add 330Ω in parallel with R1 (stock 1kΩ) → 248Ω total
- Provides 13.3mA drive current for proper saturation
- Without this: motor control unreliable or non-functional

**Firmware assumes:**
- Hopper in NEGATIVE mode (active LOW control)
- Optocoupler wiring: D1 → IN+, GND → IN-
- GPIO HIGH → LED ON → OUT LOW → motor ON
- GPIO LOW → LED OFF → OUT HIGH → motor OFF

---

## Software Requirements

### Arduino IDE Setup

1. **Install Arduino IDE** (1.8.x or 2.x)

2. **Add ESP8266 Board Support:**
   - Open: Arduino IDE → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Go to: Tools → Board Manager
   - Search: "esp8266"
   - Install: "esp8266 by ESP8266 Community"

3. **Select Board:**
   - Tools → Board → ESP8266 Boards → **"LOLIN(WEMOS) D1 R2 & mini"**

### Required Libraries

Install via **Tools → Manage Libraries** in Arduino IDE:

**Versions are pinned in `platformio.ini`, and "Latest" is not a version**
(issue #7): `.pio/libdeps/esp8266` used to hold three different async-TCP forks
and whichever ArduinoJson the day offered. Change a pin there, not here.

| Library | Owner | Pinned version | Purpose |
|---------|-------|----------------|---------|
| **ESPAsyncWebServer** | esp32async | 3.12.1 | Async HTTP server |
| **ESPAsyncTCP** | esp32async | 2.0.0 | Named although it is transitive — a range-resolved dependency is a floating one |
| **ArduinoJson** | bblanchon | 7.4.3 | JSON parsing/generation |
| *platform* | espressif8266 | 4.2.1 | …which is also the pin of the framework libraries below |

**Built-in Libraries** (no installation needed):
- ESP8266WiFi
- EEPROM or LittleFS

### Library Installation Commands

Via Arduino Library Manager (GUI):
```
Tools → Manage Libraries → Search:
1. "ESPAsyncWebServer" → Install
2. "ArduinoJson" → Install
```

Via Arduino CLI (alternative):
```bash
arduino-cli lib install "ESPAsyncWebServer"
arduino-cli lib install "ArduinoJson"
```

---

## Project Structure

```
firmware/dispenser/
├── dispenser.ino          # Main Arduino sketch
├── config.h               # WiFi, API key, pins, constants
├── http_server.cpp        # HTTP endpoints
├── http_server.h
├── dispense_manager.cpp   # Transaction state machine
├── dispense_manager.h
├── hopper_control.cpp     # Motor + sensor GPIO
├── hopper_control.h
├── flash_storage.cpp      # Persistence (EEPROM/LittleFS)
├── flash_storage.h
├── wifi_supervisor.cpp    # "restart a device nobody can reach" (issue #7)
└── wifi_supervisor.h
```

---

## Configuration

### 1. Edit config.h

```cpp
// WiFi Configuration
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define STATIC_IP "192.168.4.20"

// The shared signing secret (issue #8).  It NEVER travels: requests carry
// HMAC-SHA256 over METHOD \n PATH \n BODY \n NONCE in X-Signature.
#define SIGNING_KEY "change-this-secret-key"  // CHANGE THIS!

// GPIO Pins (Wemos D1 Mini - using D-labels)
// ⚠️ INVERTED LOGIC: Control LOW = motor ON, inputs LOW = active
#define MOTOR_PIN          D1    // GPIO5 - Control output via PC817 #1
#define COIN_PULSE_PIN     D7    // GPIO13 - Coin pulse input via PC817 #2
#define ERROR_SIGNAL_PIN   D5    // GPIO14 - Error signal input via PC817 #3
// D6 (GPIO12) is free: the hopper's empty sensor is a factory option this
// unit does not have, so the pin reported "not empty" forever (issue #6).
```

### 2. Verify Pin Connections

| ESP8266 Pin | GPIO | Function | Details |
|-------------|------|----------|---------|
| D1 | GPIO5 | Motor Control Output | Via PC817 optocoupler #1. **Wiring: D1→IN+, GND→IN-, VCC→12V**. GPIO HIGH = LED ON = OUT LOW = motor ON (with NEGATIVE mode). **⚠️ Requires R1 modification (330Ω in parallel with 1kΩ stock resistor).** Voltage thresholds: < 0.5V = motor ON, > 4V = motor OFF (protocol section 2.1) |
| D7 | GPIO13 | Coin Pulse Input | Via PC817 optocoupler #2 (active LOW). FALLING edge interrupt. Stock resistors OK. |
| D5 | GPIO14 | Error Signal Input | Via PC817 optocoupler #3 (active LOW). Stock resistors OK. |
| D6 | GPIO12 | Empty Sensor Input | Via PC817 optocoupler #4 (active LOW). Stock resistors OK. |

**Control Logic (Optocoupler-Based):**
- **Motor control:** GPIO HIGH → optocoupler LED ON → OUT LOW (< 0.5V) → motor ON (NEGATIVE mode)
- **Input signals:** All inputs are active LOW (LOW = signal detected)
- **Galvanic isolation:** PC817 modules provide electrical isolation between 12V hopper and 3.3V ESP8266
- **R1 modification critical:** Only motor control optocoupler needs modification for reliable saturation

---

## Building and Uploading

### Via Arduino IDE

1. **Open Project:**
   - File → Open → Select `dispenser/dispenser.ino`

2. **Configure Board:**
   - Tools → Board → "LOLIN(WEMOS) D1 R2 & mini"
   - Tools → Upload Speed → 115200 (or higher)
   - Tools → CPU Frequency → 80 MHz

3. **Select Port:**
   - Tools → Port → Select your Wemos D1 port
   - macOS: `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial-*`

4. **Upload:**
   - Sketch → Upload (or Ctrl+U / Cmd+U)

### Via Arduino CLI (alternative)

```bash
# Compile
arduino-cli compile --fqbn esp8266:esp8266:d1_mini dispenser/

# Upload
arduino-cli upload -p /dev/cu.usbserial-XXXX --fqbn esp8266:esp8266:d1_mini dispenser/

# Monitor serial output
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

---

## Testing

### 1. Check Serial Monitor

After upload, open Serial Monitor (115200 baud):
```
Connecting to WiFi...
Connected! IP: 192.168.4.20
HTTP server started on port 80
```

### 2. Test Health Endpoint (No Auth)

```bash
curl http://192.168.4.20/health
```

Expected response:
```json
{
  "protocol": 2,
  "state": "idle",
  "fault": "none",
  "fault_code": 0,
  "uptime": 42,
  "firmware": "1.3.0",
  "heap_free": 27512,
  "reset_reason": "Power on",
  "wifi": {"rssi": -47, "ip": "192.168.4.20", "ssid": "…", "reconnects": 0},
  "metrics": {
    "total_dispenses": 0,
    "successful": 0,
    "jams": 0
  },
  "error_history": []
}
```

`heap_free`, `reset_reason` and `wifi.reconnects` are what a bad day leaves
behind (issue #7): before them, a device that reset once a week produced one
piece of evidence — a small `uptime`.

`state` and `fault` are the whole health verdict (issue #6): `state` is
`idle | dispensing | fault`, `fault` is `none | jam | hopper_error`, and only
a power cycle clears a fault. The raw pin levels are `GET /debug` (signature
required). Unsigned, `/health` answers only `protocol`, `state`, `fault` and
`"authenticated": false` — see issue #8 and `dispenser-protocol.md`. The full contract is `dispenser-protocol.md`.

### 3. Test Authentication

Since issue #8 every protected request is **signed**. The secret never
travels; a nonce does. This helper signs one request:

```bash
KEY=your-secret-key-here
DEV=http://192.168.4.20

sign() {   # sign METHOD PATH [BODY] — sets $NONCE and $SIG
  NONCE=$(curl -s "$DEV/nonce" | sed -n 's/.*"nonce":"\([0-9a-f]*\)".*/\1/p')
  SIG=$(printf '%s\n%s\n%s\n%s' "$1" "$2" "$3" "$NONCE" \
        | openssl dgst -sha256 -hmac "$KEY" -r | cut -d' ' -f1)
}
```

`printf`, not `echo`: the canonical string has **no** trailing newline.

**Unsigned (should fail):**
```bash
curl -X POST "$DEV/dispense" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":1}'
```

Expected: `401 {"error":"unauthorized","reason":"signature"}`

**Carrying the old `X-API-Key` (should also fail):** that header is not a
credential any more and not a fallback — the request is simply unsigned.

**Signed (should work):**
```bash
BODY='{"tx_id":"test123","quantity":3}'
sign POST /dispense "$BODY"
curl -X POST "$DEV/dispense" \
  -H "X-Nonce: $NONCE" -H "X-Signature: $SIG" \
  -H "Content-Type: application/json" -d "$BODY"
```

Expected: `200 OK` with dispensing status. Sending **the same request again**
is `401 reason=nonce` — a POST spends its nonce, which is the replay defence.

### 4. Monitor Status

A read does **not** spend its nonce, so the one from the POST keeps working
for the polls behind it:

```bash
sign GET /dispense/test123
curl -H "X-Nonce: $NONCE" -H "X-Signature: $SIG" "$DEV/dispense/test123"
```

---

## Troubleshooting

### WiFi Connection Issues

- Check SSID and password in `config.h`
- Ensure 2.4GHz WiFi (ESP8266 doesn't support 5GHz)
- Check Serial Monitor for connection errors
- **The device restarts itself after a minute off the network** (issue #7),
  and never while the motor is running. A board that reboots every minute on
  the bench is telling you it cannot reach the AP — the serial line says
  `WiFi down for 60s with nothing dispensing: restarting`. It is not a crash
  loop; check `reset_reason` on the next `/health` and the credentials above.
- Modem sleep is off (`WIFI_NONE_SLEEP`). If a request takes seconds or times
  out once and succeeds on retry, check that this line survived — it is the
  usual cause on an ESP8266 that answers HTTP.

### Upload Issues

- Press RESET button on Wemos D1 during upload if it fails
- Check correct port selected
- Try lower upload speed (57600 baud)

### Library Errors

If you get compilation errors:
```
'AsyncWebServer' was not declared
```

Install missing library via Library Manager.

### Motor Control Issues

#### Motor doesn't engage during dispense

**Check hardware first:**
1. Hopper DIP switch set to NEGATIVE mode (not POSITIVE)
2. Optocoupler R1 modified (330Ω in parallel with 1kΩ)
3. VCC pin on optocoupler connected to 12V

**Verify with multimeter:**
- D1 pin HIGH during dispense: should measure 3.3V
- Optocoupler OUT LOW during dispense: should be < 0.5V to GND
- Hopper control pin (7) LOW during dispense: should be < 0.5V to GND

**If control pin HIGH when should be LOW:**
- Hopper is in POSITIVE mode → change DIP switch to NEGATIVE

**If OUT voltage too high (> 1V) when LED ON:**
- R1 not modified correctly → verify 330Ω parallel resistor
- Measure IN+ to IN-: should be ~250Ω, not 1kΩ

#### Motor engages at wrong time (after timeout instead of during dispense)

**Cause:** Hopper in POSITIVE mode (active HIGH) instead of NEGATIVE mode (active LOW).

**Fix:** Open hopper, set DIP switch to NEGATIVE, retest.

**Expected behavior with correct configuration:**
- Dispense triggered → D1 goes HIGH (3.3V)
- Optocoupler LED turns ON
- OUT drops to < 0.5V
- Motor engages immediately
- Coins dispense with pulses on D7

**See also:** [Motor Control Troubleshooting Guide](../docs/troubleshooting/motor-control-issues.md) for detailed diagnostics

### Pulse Counting Issues

- Verify D7 connected via PC817 optocoupler #2
- Check PC817 module #2 LED blinks during coin dispense
- Check pulse mode jumper on Azkoyen (STANDARD + PULSES)
- Verify interrupt configured for FALLING edge (optocoupler inverts signal)
- Monitor interrupt with Serial.println in ISR (temporarily)

---

## API Reference

See [ARCHITECTURE.md](../ARCHITECTURE.md) for complete API documentation.

**Quick Reference:**

| Endpoint | Method | Signature | Purpose |
|----------|--------|-----------|---------|
| `/nonce` | GET | no (by necessity) | Hand out a single-use nonce, 30 s |
| `/health` | GET | optional | Unsigned: `protocol`, `state`, `fault`. Signed: the full document |
| `/debug` | GET | yes | Raw pin levels, for the bench |
| `/dispense` | POST | yes, **spends the nonce** | Start a dispense transaction |
| `/dispense/{tx_id}` | GET | yes | Query transaction status |

**Authentication:** `X-Nonce` plus
`X-Signature: HMAC-SHA256(key, METHOD \n PATH \n BODY \n NONCE)`, lowercase
hex. Full specification in `dispenser-protocol.md`.

---

## Development

### Adding New Features

1. Add interface to appropriate `.h` file
2. Implement in corresponding `.cpp` file
3. Update `config.h` if new constants needed
4. Test thoroughly before deployment

### Debugging

Enable verbose output in code:
```cpp
#define DEBUG_MODE 1

#if DEBUG_MODE
  Serial.println("Debug message");
#endif
```

### Code Style

- Use consistent naming (camelCase for variables, PascalCase for functions)
- Document complex logic with comments
- Keep functions small and focused
- Use `const` for immutable values

---

## Production Deployment

### Security Checklist

- [ ] **Change SIGNING_KEY** in `config.local.h` before deployment — long and
      random; it is never typed by a person and never appears on the wire
- [ ] **Dedicated WPA2 SSID / VLAN with client isolation** (`hardware/README.md`
      § *Network*) — an installation requirement, not a recommendation
- [ ] Configure strong WiFi password
- [ ] Use static IP for predictable access
- [ ] Keep firmware updated
- [ ] Test crash recovery thoroughly

### Monitoring

- System Monitor should poll `/health` every 60 seconds
- Watch for increasing jam rates (threshold: >5%)
- Monitor success rate (threshold: <90% triggers alert)

---

## Support

- **Architecture docs:** [ARCHITECTURE.md](../ARCHITECTURE.md)
- **Protocol spec:** [dispenser-protocol.md](../dispenser-protocol.md)
- **ESP8266 Arduino Core:** https://github.com/esp8266/Arduino
- **ESPAsyncWebServer:** https://github.com/me-no-dev/ESPAsyncWebServer

---

## License

See repository root for license information.
