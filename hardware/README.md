# Hardware Setup Guide

This guide covers the physical hardware assembly for the Remote Token Dispenser system.

---

## 📦 Components

### Core Hardware

| Component | Model | Purpose | Cost Estimate |
|-----------|-------|---------|---------------|
| **Coin Dispenser** | Azkoyen Hopper U-II (used) | Industrial token/coin dispensing | ~€30 |
| **WiFi Controller** | Wemos D1 Mini (ESP8266) | HTTP server, state machine | ~€5 |
| **Power Supply** | 12V/2A DC adapter | Hopper motor power | ~€10 |
| **Level Shifter** | 3.3V → 12V relay module | Motor control interface | ~€3 |
| **Capacitor** | 2200µF 25V electrolytic | Motor startup surge protection | ~€1 |

### Supporting Hardware

- Jumper wires (male-to-female, 20cm)
- USB cable (for ESP8266 programming)
- Enclosure/junction box (optional, for protection)
- Wire terminals and connectors

**Total Cost:** ~€50-60 (excluding enclosure)

---

## 🔌 Wiring Diagrams

### Main Wiring: ESP8266 ↔ Hopper

![Wiring Diagram](../docs/wiring-diagram.svg)

**Key connections:**
- **D5 (GPIO14)** → Motor control (via relay/level shifter)
- **D6 (GPIO12)** → Coin pulse sensor (interrupt-driven counting)
- **D8 (GPIO15)** → Hopper low sensor (optional, detects empty hopper)
- **GND** → Common ground (critical!)

[See complete diagram](../docs/wiring-diagram.svg)

---

### Pinout Reference: Wemos D1 Mini

![Pinout Diagram](../docs/pinout-diagram.svg)

**Used pins highlighted in red:**
- D5 (GPIO14) - Motor control output
- D6 (GPIO12) - Coin pulse interrupt input
- D8 (GPIO15) - Hopper low sensor input

[See complete pinout](../docs/pinout-diagram.svg)

---

### Power Supply Wiring

![Power Diagram](../docs/power-diagram.svg)

**Critical power requirements:**
- **Separate 12V supply** for hopper (NOT from ESP8266)
- **Common ground** between ESP8266 and hopper (required!)
- **2200µF capacitor** on 12V line stabilizes motor startup current surge
- ESP8266 powered via USB (5V)

[See complete power diagram](../docs/power-diagram.svg)

---

## 🔧 Assembly Instructions

### Step 1: Configure the Hopper

The Azkoyen Hopper U-II must be configured in **PULSES** mode:

1. Open the hopper (4 screws on top panel)
2. Locate the DIP switches inside (usually near the motor)
3. Set to **PULSES** mode (30ms pulse per coin)
   - Refer to [Azkoyen U-II manual](https://www.casino-software.de/download/hopper-azkoyen-u2-manual.pdf) for exact switch positions
4. Close and test manually (should click 30ms per coin)

### Step 2: Wire the Power Supply

**⚠️ POWER OFF during wiring!**

1. Connect **12V PSU ground** to **hopper GND** pin
2. Connect **12V PSU positive** to **hopper 12V** pin
3. Solder **2200µF capacitor** across 12V and GND (observe polarity!)
   - Negative stripe on capacitor → GND
   - Positive lead → 12V
4. Keep ESP8266 powered separately via USB

### Step 3: Wire the Control Signals

1. **Motor control (D5 → Hopper Motor Enable):**
   - Connect D5 to relay input
   - Connect relay output to hopper "Motor Enable" pin
   - Relay switches between 3.3V (ESP) and 12V (hopper)

2. **Coin pulse sensor (Hopper Pulse Out → D6):**
   - Direct connection: Hopper "Pulse Out" → D6
   - 30ms LOW pulse per dispensed token
   - Use FALLING edge interrupt in firmware

3. **Hopper low sensor (Hopper Low → D8):**
   - Optional: Hopper "Hopper Low" → D8
   - Active LOW when hopper empty
   - Use INPUT_PULLUP mode

4. **Common ground:**
   - Connect ESP8266 GND to hopper GND
   - **This is critical for signal integrity!**

### Step 4: Test the Wiring

Before powering everything on:

1. **Visual inspection:**
   - Check all connections are secure
   - Verify capacitor polarity (explosion risk if reversed!)
   - Ensure no shorts between 12V and GND

2. **Multimeter test:**
   - Measure 12V supply voltage (should be 11.5-12.5V)
   - Check continuity of ground connections
   - Verify relay switches correctly

3. **Power-on test:**
   - Power ESP8266 via USB first
   - Then power the 12V supply
   - ESP8266 should boot and connect to WiFi
   - No smoke, burning smell, or unusual heat

4. **Dispense test:**
   - Send test dispense via HTTP API
   - Motor should activate
   - Tokens should dispense
   - Pulse count should match dispensed tokens

---

## 🛡️ Safety Considerations

### Electrical Safety

- **Never** power the hopper motor from ESP8266 pins (insufficient current!)
- **Always** use a relay or level shifter for motor control
- **Check** capacitor polarity before powering on
- **Use** proper wire gauge for 12V/2A current (minimum 20 AWG)
- **Fuse** the 12V line (2A fast-blow fuse recommended)

### Mechanical Safety

- **Secure** the hopper firmly (vibration during dispense)
- **Cover** exposed 12V connections (electrical tape or heat shrink)
- **Protect** against moisture (IP54+ enclosure if outdoors)
- **Test** thoroughly before deploying with real currency

### Token Safety

- Use tokens/coins that match hopper specifications
- Avoid corroded or damaged tokens (jam risk)
- Keep hopper clean (dust/debris causes jams)

---

## 🐛 Troubleshooting

### Motor doesn't activate

- **Check:** Relay wiring (should click when D5 goes HIGH)
- **Check:** 12V power supply voltage
- **Check:** Common ground connection

### Pulse count doesn't increment

- **Check:** D6 connection to "Pulse Out" pin
- **Check:** Hopper is configured in PULSES mode (not LEVEL)
- **Check:** Firmware interrupt is configured for FALLING edge

### Dispense jams frequently

- **Check:** Tokens are clean and not damaged
- **Check:** Hopper is properly calibrated (see manual)
- **Check:** 2200µF capacitor is installed (motor surge protection)
- **Clean:** Hopper interior and opto-sensors

### ESP8266 reboots during dispense

- **Check:** Power supply has sufficient current (2A minimum)
- **Check:** Capacitor is present and functional
- **Check:** ESP8266 has separate power source (not sharing 12V)

---

## 📚 Additional Resources

- **[Azkoyen Hopper U-II Manual](https://www.casino-software.de/download/hopper-azkoyen-u2-manual.pdf)** - Complete hopper documentation
- **[Wemos D1 Mini Pinout](https://www.mischianti.org/wp-content/uploads/2021/06/WeMos-D1-mini-pinout-mischianti-1024x655.png)** - Detailed pin reference
- **[ESP8266 GPIO Documentation](https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/)** - GPIO capabilities and limitations
- **[Diagram Generation](../docs/README-diagrams.md)** - How to regenerate wiring diagrams

---

## 🔄 Regenerating Diagrams

All wiring diagrams are code-generated using Python for version control and reproducibility.

To regenerate the diagrams:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install schemdraw
python docs/generate_diagrams.py
```

See [docs/README-diagrams.md](../docs/README-diagrams.md) for details.

---

## 📸 Photos

<p align="center">
  <img src="../docs/hopper.jpg" alt="Azkoyen Hopper U-II" width="400"/>
  <br>
  <em>Azkoyen Hopper U-II - Industrial coin/token dispenser</em>
  <br><br>
  <img src="../docs/wemosd1.jpg" alt="Wemos D1 Mini" width="300"/>
  <br>
  <em>Wemos D1 Mini - ESP8266 WiFi controller</em>
</p>

---

## ✅ Assembly Checklist

Before deploying your token dispenser:

- [ ] Hopper configured in PULSES mode (30ms pulses)
- [ ] 12V power supply connected with 2200µF capacitor
- [ ] Common ground connected between ESP8266 and hopper
- [ ] Relay/level shifter installed for motor control (3.3V → 12V)
- [ ] D5 → Motor Enable (via relay)
- [ ] D6 → Pulse Out (direct connection)
- [ ] D8 → Hopper Low (optional, direct connection)
- [ ] All connections visually inspected and tested with multimeter
- [ ] Capacitor polarity verified (critical!)
- [ ] ESP8266 firmware flashed and WiFi configured
- [ ] Test dispense successful (motor runs, tokens dispense, pulses count)
- [ ] API authentication configured (API key set)
- [ ] System tested with 10+ dispenses (no jams, correct counts)
- [ ] Enclosure secured and weatherproofed (if applicable)

---

**Ready to assemble?** Follow the steps above and refer to the wiring diagrams. If you encounter issues, check the troubleshooting section or consult the hopper manual.

**Happy building! 🔧**
