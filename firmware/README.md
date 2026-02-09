# ESP8266 Token Dispenser Firmware

HTTP-controlled token dispenser firmware for Wemos D1 Mini (ESP8266) controlling an Azkoyen Hopper U-II.

---

## Hardware Requirements

- **Board:** Wemos D1 Mini (ESP8266) - LOLIN(WEMOS) D1 R2 & mini
- **Dispenser:** Azkoyen Hopper U-II (configured in PULSES mode)
- **Power:**
  - ESP8266: 5V via USB or VIN pin
  - Azkoyen Hopper: 12V/2A separate power supply
- **Control Circuit:** BC547 NPN transistor with 1kΩ base resistor and 10kΩ pull-up
- **Input Protection:** 3× voltage dividers (10kΩ/3.3kΩ) for Coin, Error, Empty signals

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
#define CONTROL_PIN        D1    // GPIO5 - Control output via BC547
#define COIN_PULSE_PIN     D2    // GPIO4 - Coin pulse input
#define ERROR_SIGNAL_PIN   D5    // GPIO14 - Error signal input
#define EMPTY_SENSOR_PIN   D6    // GPIO12 - Empty sensor input
```

### 2. Verify Pin Connections

| ESP8266 Pin | GPIO | Function | Azkoyen Connection |
|-------------|------|----------|-------------------|
| D1 | GPIO5 | Control Output | Via BC547 transistor (1kΩ + 10kΩ pull-up) → Control pin |
| D2 | GPIO4 | Coin Pulse Input | Coin pin → Voltage divider (10kΩ/3.3kΩ) |
| D5 | GPIO14 | Error Signal Input | Error pin → Voltage divider (10kΩ/3.3kΩ) |
| D6 | GPIO12 | Empty Sensor Input | Empty pin → Voltage divider (10kΩ/3.3kΩ) |

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

### Motor Not Running

- Check D1 → BC547 transistor wiring (1kΩ base, 10kΩ pull-up)
- Verify D1 goes HIGH when dispense starts
- Check BC547 collector pulls Control pin LOW when active
- Verify 12V power supply to Azkoyen
- Measure: D1=3.3V, BC547 base=0.7V, collector=0V when ON

### Pulse Counting Issues

- Verify D2 connected to Coin pin via voltage divider (10kΩ/3.3kΩ)
- Check voltage at D2: should be ~2.98V when Coin pin is HIGH
- Check pulse mode jumper on Azkoyen (STANDARD + PULSES)
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
