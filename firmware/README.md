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

| Library | Author | Version | Purpose |
|---------|--------|---------|---------|
| **ESPAsyncWebServer** | me-no-dev | Latest | Async HTTP server |
| **ESPAsyncTCP** | me-no-dev | Latest | Auto-installed with above |
| **ArduinoJson** | Benoit Blanchon | 7.x (7.0.0+) | JSON parsing/generation |

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
└── flash_storage.h
```

---

## Configuration

### 1. Edit config.h

```cpp
// WiFi Configuration
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define STATIC_IP "192.168.4.20"

// API Authentication
#define API_KEY "change-this-secret-key"  // CHANGE THIS!

// GPIO Pins (Wemos D1 Mini - using D-labels)
// ⚠️ INVERTED LOGIC: Control LOW = motor ON, inputs LOW = active
#define MOTOR_PIN          D1    // GPIO5 - Control output via PC817 #1
#define COIN_PULSE_PIN     D7    // GPIO13 - Coin pulse input via PC817 #2
#define ERROR_SIGNAL_PIN   D5    // GPIO14 - Error signal input via PC817 #3
#define HOPPER_LOW_PIN     D6    // GPIO12 - Empty sensor input via PC817 #4
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
  "status": "ok",
  "uptime": 42,
  "firmware": "1.0.0",
  "dispenser": "idle",
  "hopper_low": false,
  "metrics": {
    "total_dispenses": 0,
    "successful": 0,
    "jams": 0
  }
}
```

### 3. Test Authentication

**Without API key (should fail):**
```bash
curl -X POST http://192.168.4.20/dispense \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":1}'
```

Expected: `401 Unauthorized`

**With API key (should work):**
```bash
curl -X POST http://192.168.4.20/dispense \
  -H "X-API-Key: your-secret-key-here" \
  -H "Content-Type: application/json" \
  -d '{"tx_id":"test123","quantity":3}'
```

Expected: `200 OK` with dispensing status

### 4. Monitor Status

```bash
curl -H "X-API-Key: your-secret-key-here" \
  http://192.168.4.20/dispense/test123
```

---

## Troubleshooting

### WiFi Connection Issues

- Check SSID and password in `config.h`
- Ensure 2.4GHz WiFi (ESP8266 doesn't support 5GHz)
- Check Serial Monitor for connection errors

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

| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/health` | GET | No | Health status & metrics |
| `/dispense` | POST | Yes | Start dispense transaction |
| `/dispense/{tx_id}` | GET | Yes | Query transaction status |

**Authentication:** Include header `X-API-Key: your-secret-key-here`

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

- [ ] **Change API_KEY** in `config.h` before deployment
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
