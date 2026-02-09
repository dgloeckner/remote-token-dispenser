#!/usr/bin/env python3
"""
Generate wiring diagrams for the Remote Token Dispenser project.

Requirements:
    pip install schemdraw

Usage:
    python docs/generate_diagrams.py

Outputs:
    - docs/wiring-diagram.svg
    - docs/pinout-diagram.svg
    - docs/power-diagram.svg
"""

import schemdraw
from schemdraw import elements as elm
from schemdraw.segments import Segment, SegmentText


def create_wiring_diagram():
    """Create main wiring diagram: ESP8266 to Azkoyen Hopper U-II with accurate circuit."""

    with schemdraw.Drawing(show=False, canvas='svg') as d:
        d.config(fontsize=11, font='sans-serif', bgcolor='white')

        # Title
        d += elm.Label().label('Azkoyen Hopper U-II ↔ Wemos D1 Mini Wiring', fontsize=16, loc='top')
        d.push()

        d.move(0, -2.5)

        # Wemos D1 Mini - Left side
        d += (esp := elm.Ic(pins=[
            elm.IcPin(name='D1 (GPIO5)\nControl', side='right', pin='1', anchorname='d1'),
            elm.IcPin(name='D2 (GPIO4)\nCoin', side='right', pin='2', anchorname='d2'),
            elm.IcPin(name='D5 (GPIO14)\nError', side='right', pin='3', anchorname='d5'),
            elm.IcPin(name='D6 (GPIO12)\nEmpty', side='right', pin='4', anchorname='d6'),
            elm.IcPin(name='GND', side='right', pin='5', anchorname='gnd'),
            elm.IcPin(name='5V (USB)', side='left', pin='6'),
        ], w=5, pinspacing=1.6, edgepadH=1.2, label='Wemos D1 Mini\nESP8266', lblofst=0))

        # Control output (D1 → BC547 NPN transistor)
        d.move_from(esp.d1, dx=0.5)
        d += elm.Line().right(0.8)
        d += elm.Resistor().right().label('R1\n1kΩ', loc='top')
        d += elm.Line().right(0.3)
        d += elm.Dot()
        base_node = d.here

        # BC547 transistor
        d += elm.Line().down(0.8)
        d += (bjt := elm.Bjt(circle=True).label('BC547', loc='right'))
        d.move_from(bjt.collector)
        d += elm.Line().up(0.5)
        d += elm.Dot()
        collector_node = d.here

        # Pull-up resistor to 12V
        d += elm.Resistor().up().label('R2\n10kΩ', loc='left')
        d += elm.Line().up(0.3)
        d += elm.Dot().label('+12V', loc='top')

        # Emitter to ground
        d.move_from(bjt.emitter)
        d += elm.Line().down(0.3)
        d += elm.Ground()

        # Collector to hopper Control pin
        d.move_from(collector_node)
        d += elm.Line().right(2)
        control_to_hopper = d.here

        # Coin input (Hopper → voltage divider → D2)
        d.move_from(esp.d2, dx=0.5)
        d += elm.Line().right(1.5)
        d += elm.Dot()
        coin_div_top = d.here
        d += elm.Resistor().right().label('R3: 10kΩ', loc='top', ofst=0.1)
        d += elm.Line().right(0.5)
        coin_from_hopper = d.here

        # Coin voltage divider bottom
        d.move_from(coin_div_top)
        d += elm.Line().down(0.8)
        d += elm.Resistor().down().label('R4\n3.3kΩ', loc='right')
        d += elm.Line().down(0.3)
        d += elm.Ground()

        # Error input (Hopper → voltage divider → D5)
        d.move_from(esp.d5, dx=0.5)
        d += elm.Line().right(1.5)
        d += elm.Dot()
        error_div_top = d.here
        d += elm.Resistor().right().label('R5: 10kΩ', loc='top', ofst=0.1)
        d += elm.Line().right(0.5)
        error_from_hopper = d.here

        # Error voltage divider bottom
        d.move_from(error_div_top)
        d += elm.Line().down(0.8)
        d += elm.Resistor().down().label('R6\n3.3kΩ', loc='right')
        d += elm.Line().down(0.3)
        d += elm.Ground()

        # Empty input (Hopper → voltage divider → D6)
        d.move_from(esp.d6, dx=0.5)
        d += elm.Line().right(1.5)
        d += elm.Dot()
        empty_div_top = d.here
        d += elm.Resistor().right().label('R7: 10kΩ', loc='top', ofst=0.1)
        d += elm.Line().right(0.5)
        empty_from_hopper = d.here

        # Empty voltage divider bottom
        d.move_from(empty_div_top)
        d += elm.Line().down(0.8)
        d += elm.Resistor().down().label('R8\n3.3kΩ', loc='right')
        d += elm.Line().down(0.3)
        d += elm.Ground()

        # Common ground
        d.move_from(esp.gnd, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Dot().label('Common\nGND', loc='bottom')
        common_gnd = d.here
        d += elm.Line().right(5)
        gnd_to_hopper = d.here

        # Azkoyen Hopper U-II on right side
        d.move_from(control_to_hopper, dx=1, dy=2)
        d += (hopper := elm.Ic(pins=[
            elm.IcPin(name='VCC', side='top', pin='1', anchorname='vcc1'),
            elm.IcPin(name='VCC', side='top', pin='2', anchorname='vcc2'),
            elm.IcPin(name='Control', side='left', pin='3', anchorname='ctrl'),
            elm.IcPin(name='Coin', side='left', pin='4', anchorname='coin'),
            elm.IcPin(name='Error', side='left', pin='5', anchorname='error'),
            elm.IcPin(name='Empty', side='left', pin='6', anchorname='empty'),
            elm.IcPin(name='GND', side='bottom', pin='7', anchorname='hgnd1'),
            elm.IcPin(name='GND', side='bottom', pin='8', anchorname='hgnd2'),
        ], w=4, pinspacing=1.4, edgepadH=1.0, label='Azkoyen Hopper U-II', lblofst=-0.8))

        # Connect signals to hopper
        d.move_from(control_to_hopper)
        d += elm.Line().to(hopper.ctrl)

        d.move_from(coin_from_hopper)
        d += elm.Line().to(hopper.coin)

        d.move_from(error_from_hopper)
        d += elm.Line().to(hopper.error)

        d.move_from(empty_from_hopper)
        d += elm.Line().to(hopper.empty)

        # 12V power to hopper
        d.move_from(hopper.vcc1, dy=0.5)
        d += elm.Line().up(0.5)
        d += elm.Dot().label('+12V', loc='top')

        # Hopper ground to common
        d.move_from(hopper.hgnd1, dy=-0.5)
        d += elm.Line().down(0.5)
        d += elm.Dot()
        d += elm.Line().to(gnd_to_hopper)

    d.save('docs/wiring-diagram.svg')
    print('✓ Generated docs/wiring-diagram.svg')


def create_pinout_diagram():
    """Create Wemos D1 Mini pinout reference diagram."""

    with schemdraw.Drawing(show=False, canvas='svg') as d:
        d.config(fontsize=11, font='sans-serif', bgcolor='white')

        # Title
        d += elm.Label().label('Wemos D1 Mini Pinout Reference', fontsize=16, loc='top')
        d.push()

        d.move(0, -2.5)

        # Left pins
        left_pins = [
            ('RST', 'Reset'),
            ('A0', 'Analog Input'),
            ('D0', 'GPIO16'),
            ('D5', 'GPIO14 → Motor'),
            ('D6', 'GPIO12 ← Pulse'),
            ('D7', 'GPIO13 (unused)'),
            ('D8', 'GPIO15 ← Low'),
            ('3V3', '3.3V Output'),
        ]

        # Right pins
        right_pins = [
            ('TX', 'GPIO1 (Serial)'),
            ('RX', 'GPIO3 (Serial)'),
            ('D1', 'GPIO5 (I2C SCL)'),
            ('D2', 'GPIO4 (I2C SDA)'),
            ('D3', 'GPIO0 (unused)'),
            ('D4', 'GPIO2 (LED)'),
            ('GND', 'Ground'),
            ('5V', '5V Input (USB)'),
        ]

        # Draw board
        d += (board := elm.Ic(pins=[
            elm.IcPin(name=f'{pin}\n{desc}', side='left', pin=str(i),
                     anchorname=pin.lower(),
                     color='red' if 'Motor' in desc or 'Pulse' in desc or 'Low' in desc else 'black')
            for i, (pin, desc) in enumerate(left_pins, 1)
        ] + [
            elm.IcPin(name=f'{pin}\n{desc}', side='right', pin=str(i+len(left_pins)))
            for i, (pin, desc) in enumerate(right_pins, 1)
        ], w=8, pinspacing=1.4, edgepadH=1.2, label='WEMOS D1 MINI\nESP8266', lblofst=0))

    d.save('docs/pinout-diagram.svg')
    print('✓ Generated docs/pinout-diagram.svg')


def create_power_diagram():
    """Create power supply wiring diagram."""

    with schemdraw.Drawing(show=False, canvas='svg') as d:
        d.config(fontsize=12, font='sans-serif', bgcolor='white')

        # Title
        d += elm.Label().label('Power Supply Wiring', fontsize=16, loc='top')
        d.push()

        d.move(0, -2.5)

        # 12V Power Supply
        d += elm.Label().label('AC 110-240V', loc='left')
        d += elm.Line().right(1)
        d += (psu := elm.Ic(pins=[
            elm.IcPin(name='+12V', side='right', pin='1', anchorname='v12'),
            elm.IcPin(name='GND', side='right', pin='2', anchorname='gnd'),
        ], w=3.0, pinspacing=1.5, label='12V/2A\nPower Supply', lblofst=0))

        # 12V to voltage regulator for ESP8266
        d.move_from(psu.v12, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Dot()
        v12_split = d.here
        d += elm.Line().up(2)
        d += elm.Line().right(0.5)
        d += (vreg := elm.Ic(pins=[
            elm.IcPin(name='12V', side='left', pin='1'),
            elm.IcPin(name='5V', side='right', pin='2', anchorname='v5'),
            elm.IcPin(name='GND', side='bottom', pin='3', anchorname='vgnd'),
        ], w=2.5, pinspacing=1.2, label='Voltage\nRegulator', lblofst=0))

        # Regulator to ESP8266
        d.move_from(vreg.v5, dx=0.5)
        d += elm.Line().right(0.5)
        d += (esp := elm.Ic(pins=[
            elm.IcPin(name='5V', side='left', pin='1'),
            elm.IcPin(name='GND', side='left', pin='2', anchorname='egnd'),
        ], w=3.0, pinspacing=1.5, label='ESP8266\n(Wemos D1)', lblofst=0))

        # ESP8266 GND
        d.move_from(esp.egnd, dx=-0.5)
        d += elm.Line().left(0.5)
        d += elm.Dot()
        esp_gnd = d.here

        # Voltage regulator GND
        d.move_from(vreg.vgnd, dy=-0.5)
        d += elm.Line().down(0.5)
        d += elm.Dot().label('GND', loc='left')
        gnd_common = d.here

        # Connect ESP GND to common
        d.move_from(esp_gnd)
        d += elm.Line().to(gnd_common)

        # 12V split to Hopper
        d.move_from(v12_split)
        d += elm.Line().right(1)
        d += elm.Dot()
        v12_node = d.here
        d += elm.Line().right(1.5)
        d += (hopper := elm.Ic(pins=[
            elm.IcPin(name='12V', side='left', pin='1'),
            elm.IcPin(name='GND', side='left', pin='2', anchorname='hgnd'),
            elm.IcPin(name='Motor', side='right', pin='3'),
        ], w=5.0, pinspacing=2.0, label='Azkoyen\nHopper', lblofst=-0.3))

        # Capacitor on 12V line (connected between 12V and GND)
        d.move_from(v12_node, dx=0, dy=-0.5)
        d += elm.Line().down(0.3)
        d += elm.Capacitor().down().label('2200µF\n25V', loc='right')
        d += elm.Line().down(0.3)
        cap_gnd = d.here

        # Hopper GND to common
        d.move_from(hopper.hgnd, dx=-0.5)
        d += elm.Line().left(1)
        d += elm.Dot().label('GND', loc='bottom')
        hopper_gnd = d.here

        # Capacitor GND to common
        d.move_from(cap_gnd)
        d += elm.Line().to(hopper_gnd)

        # Common GND connection
        d.move_from(gnd_common)
        d += elm.Line().to(hopper_gnd)

        # 12V PSU GND to common
        d.move_from(psu.gnd, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Line().to(gnd_common)

    d.save('docs/power-diagram.svg')
    print('✓ Generated docs/power-diagram.svg')


def main():
    """Generate all diagrams."""
    print('Generating wiring diagrams...\n')

    try:
        create_wiring_diagram()
        create_pinout_diagram()
        create_power_diagram()

        print('\n✅ All diagrams generated successfully!')
        print('\nGenerated files:')
        print('  - docs/wiring-diagram.svg  (Main ESP8266 ↔ Hopper wiring)')
        print('  - docs/pinout-diagram.svg  (Wemos D1 Mini pinout reference)')
        print('  - docs/power-diagram.svg   (Power supply connections)')
        print('\nAdd to your documentation with:')
        print('  ![Wiring Diagram](docs/wiring-diagram.svg)')

    except ImportError:
        print('❌ Error: schemdraw library not found')
        print('\nInstall with: pip install schemdraw')
        print('Then run: python docs/generate_diagrams.py')
        return 1
    except Exception as e:
        print(f'❌ Error generating diagrams: {e}')
        return 1

    return 0


if __name__ == '__main__':
    exit(main())
