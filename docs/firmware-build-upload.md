# Firmware Build and Upload Guide

This guide covers building and uploading the ESP8266 firmware using `arduino-cli`.

## Prerequisites

- `arduino-cli` installed ([installation guide](https://arduino.github.io/arduino-cli/latest/installation/))
- Wemos D1 Mini (or compatible ESP8266 board)
- USB cable for programming

## One-Time Setup

### 1. Install ESP8266 Board Support

```bash
# Add ESP8266 board index
arduino-cli config init
arduino-cli config add board_manager.additional_urls http://arduino.esp8266.com/stable/package_esp8266com_index.json

# Update board index
arduino-cli core update-index

# Install ESP8266 core
arduino-cli core install esp8266:esp8266
```

### 2. Install Required Libraries

```bash
# Install ArduinoJson (v7.x)
arduino-cli lib install "ArduinoJson"

# Install ESPAsyncWebServer and dependencies
arduino-cli lib install "ESPAsyncTCP"
# Note: ESPAsyncWebServer may need manual installation - see below
```

**Manual ESPAsyncWebServer Installation (if needed):**

If `arduino-cli lib install "ESPAsyncWebServer"` fails, install manually:

```bash
cd ~/Arduino/libraries
git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
```

### 3. Create Local Configuration

Copy the example config and customize it:

```bash
cd firmware/dispenser
cp config.local.h.example config.local.h
```

Edit `config.local.h` with your WiFi credentials and API key:

```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourWiFiPassword"
#define STATIC_IP IPAddress(192, 168, 4, 20)
#define GATEWAY IPAddress(192, 168, 4, 1)
#define SUBNET IPAddress(255, 255, 255, 0)
#define API_KEY "your-secret-api-key-here"
```

## Building

### Compile Firmware

```bash
cd firmware/dispenser
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .
```

**Expected output:**
```
Sketch uses XXXXX bytes (XX%) of program storage space.
Global variables use XXXXX bytes (XX%) of dynamic memory.
```

### Clean Build (if needed)

```bash
arduino-cli compile --clean --fqbn esp8266:esp8266:d1_mini .
```

## Uploading

### 1. Find Serial Port

```bash
arduino-cli board list
```

**Example output:**
```
Port         Protocol Type              Board Name FQBN            Core
/dev/cu.usbserial-XXXX serial   Serial Port (USB) Unknown
```

Common port names:
- **macOS**: `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`
- **Linux**: `/dev/ttyUSB0` or `/dev/ttyACM0`
- **Windows**: `COM3`, `COM4`, etc.

### 2. Upload Firmware

Replace `/dev/cu.usbserial-XXXX` with your actual port:

```bash
arduino-cli upload --fqbn esp8266:esp8266:d1_mini --port /dev/cu.usbserial-XXXX .
```

**Upload options:**

```bash
# Upload with verbose output
arduino-cli upload --fqbn esp8266:esp8266:d1_mini --port /dev/cu.usbserial-XXXX --verbose .

# Compile and upload in one step
arduino-cli compile --upload --fqbn esp8266:esp8266:d1_mini --port /dev/cu.usbserial-XXXX .
```

## Monitoring

### Serial Monitor

Monitor serial output at 9600 baud:

```bash
arduino-cli monitor --port /dev/cu.usbserial-XXXX --config baudrate=9600
```

**Alternative: screen (macOS/Linux)**

```bash
screen /dev/cu.usbserial-XXXX 9600
```

To exit screen: Press `Ctrl+A`, then `K`, then `Y`

**Alternative: minicom (Linux)**

```bash
minicom -D /dev/ttyUSB0 -b 9600
```

### Expected Serial Output

On successful boot, you should see:

```
=== Token Dispenser Starting ===
Firmware: 1.1.0-DEBUG-error-decoding
Connecting to WiFi: YourNetworkName
.....
WiFi connected!
IP address: 192.168.4.20
>>> DEBUG: Continuing setup...
>>> DEBUG: Initializing flash storage...
>>> DEBUG: About to call hopperControl.begin()...
[HopperControl] Initializing...
[HopperControl] MOTOR_PIN (D1) configured as OUTPUT, set to LOW (motor OFF)
[HopperControl] Input pins configured with INPUT_PULLUP
[HopperControl] Interrupt attached to COIN_PULSE_PIN (FALLING edge)
[HopperControl] Interrupt attached to ERROR_SIGNAL_PIN (CHANGE edge)
>>> DEBUG: hopperControl.begin() completed
Hopper control initialized
Dispense manager initialized
State: IDLE
Setup complete
```

## Verification

### 1. Check WiFi Connection

Ping the device:

```bash
ping 192.168.4.20
```

### 2. Test Health Endpoint

```bash
curl http://192.168.4.20/health | jq
```

**Expected response:**

```json
{
  "status": "ok",
  "uptime": 123,
  "firmware": "1.1.0-DEBUG-error-decoding",
  "wifi": {
    "rssi": -45,
    "ip": "192.168.4.20",
    "ssid": "YourNetworkName"
  },
  "dispenser": "idle",
  "gpio": { ... },
  "metrics": { ... },
  "error": {
    "active": false
  },
  "error_history": []
}
```

### 3. Verify Error Decoding (Serial Monitor)

If you have errors, you should see log messages:

```
[ErrorDecoder] Error detected: JAM_PERMANENT - Permanent jam detected
[ErrorHistory] Added error: JAM_PERMANENT at timestamp 12345
```

## Troubleshooting

### Upload Fails: Permission Denied (Linux)

Add your user to the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Upload Fails: Port in Use

Close any serial monitors or other programs using the port.

### Compilation Errors

**Missing libraries:**
```bash
arduino-cli lib install "LibraryName"
```

**Board not found:**
```bash
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

### WiFi Won't Connect

1. Verify credentials in `config.local.h`
2. Check IP address doesn't conflict with another device
3. Ensure WiFi network is 2.4GHz (ESP8266 doesn't support 5GHz)

### Device Not Responding

1. Check power supply (12V/2A for hopper + ESP8266)
2. Verify wiring connections
3. Check serial monitor for error messages
4. Try power cycling the device

## Quick Reference

```bash
# One-command build and upload
cd firmware/dispenser
arduino-cli compile --upload --fqbn esp8266:esp8266:d1_mini --port /dev/cu.usbserial-XXXX .

# Monitor serial output
arduino-cli monitor --port /dev/cu.usbserial-XXXX --config baudrate=9600

# Test endpoint
curl http://192.168.4.20/health | jq '.firmware, .error'
```

## Board Configuration

The firmware is configured for **Wemos D1 Mini** with these settings:

- **FQBN**: `esp8266:esp8266:d1_mini`
- **Baud Rate**: 9600 (serial monitor)
- **Upload Speed**: 115200 (default)
- **Flash Size**: 4MB (default on D1 Mini)

For other ESP8266 boards, adjust the FQBN accordingly:
- NodeMCU: `esp8266:esp8266:nodemcuv2`
- Generic ESP8266: `esp8266:esp8266:generic`

## Advanced: OTA Updates (Future)

Over-the-Air updates are not yet implemented but can be added using ArduinoOTA library for easier remote updates without USB cable.
