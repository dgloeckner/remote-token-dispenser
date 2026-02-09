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
    """Create main wiring diagram: ESP8266 to Azkoyen Hopper U-II."""

    with schemdraw.Drawing(show=False, canvas='svg') as d:
        d.config(fontsize=12, font='sans-serif', bgcolor='white')

        # Title
        d += elm.Label().label('ESP8266 ↔ Azkoyen Hopper U-II Wiring', fontsize=16, loc='top')
        d.push()

        # ESP8266 (Wemos D1 Mini) - Left side
        d.move(0, -2.5)
        d += (esp := elm.Ic(pins=[
            elm.IcPin(name='5V', side='left', pin='1'),
            elm.IcPin(name='GND', side='left', pin='2'),
            elm.IcPin(name='D5\n(GPIO14)', side='right', pin='3', anchorname='d5'),
            elm.IcPin(name='D6\n(GPIO12)', side='right', pin='4', anchorname='d6'),
            elm.IcPin(name='D8\n(GPIO15)', side='right', pin='5', anchorname='d8'),
        ], w=5, pinspacing=1.8, edgepadH=1.0, label='Wemos D1 Mini\n(ESP8266)', lblofst=0))

        # Motor control path (D5 → Relay → Motor)
        d.move_from(esp.d5, dx=0.5)
        d += elm.Line().right(1).label('3.3V', loc='top', ofst=0.2)
        d += (relay := elm.Relay().right().label('Relay/\nLevel Shifter', loc='bottom', ofst=0.5))
        d += elm.Line().right(1).label('12V', loc='top', ofst=0.2)
        d += elm.Dot()
        motor_x = d.here[0]
        d += elm.Line().right(1.5)
        d += elm.Label().label('Motor Enable', loc='right')

        # Pulse signal (D6 ← Pulse Out)
        d.move_from(esp.d6, dx=0.5)
        d += elm.Line().right(1)
        d += elm.Dot()
        d += elm.Line().right(2.5)
        d += elm.Line().right(1.5)
        d += elm.Label().label('Pulse Out\n(30ms pulses)', loc='right')

        # Hopper low sensor (D8 ← Hopper Low)
        d.move_from(esp.d8, dx=0.5)
        d += elm.Line().right(1)
        d += elm.Dot()
        d += elm.Line().right(2.5)
        d += elm.Line().right(1.5)
        d += elm.Label().label('Hopper Low\n(optional)', loc='right')

        # Ground connections
        d.move_from(esp.pin2, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Dot()
        d += elm.Line().down(0.5)
        d += elm.Ground()

        # Common ground to hopper
        d += elm.Line().right(2.5)
        d += elm.Label().label('Common GND', loc='right')

        # Hopper box on right side
        d.move_from(esp.d5, dx=7)
        d += elm.Ic(pins=[
            elm.IcPin(name='Motor\nEnable', side='left', pin='1'),
            elm.IcPin(name='Pulse\nOut', side='left', pin='2'),
            elm.IcPin(name='Hopper\nLow', side='left', pin='3'),
            elm.IcPin(name='GND', side='left', pin='4'),
            elm.IcPin(name='12V\nPower', side='top', pin='5'),
        ], w=4.0, pinspacing=1.8, edgepadH=1.0, label='Azkoyen\nHopper U-II', lblofst=0)

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

        # USB Power to ESP8266
        d += elm.Label().label('USB', loc='left')
        d += elm.Line().right(1)
        d += elm.Ic(pins=[
            elm.IcPin(name='5V', side='right', pin='1', anchorname='5v'),
            elm.IcPin(name='GND', side='right', pin='2', anchorname='gnd'),
        ], w=3.0, pinspacing=1.5, label='ESP8266\n(Wemos D1)', lblofst=0)

        # ESP8266 GND
        d.move_from(d.elements[-1].gnd, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Dot().label('GND', loc='bottom')
        gnd_common = d.here

        # 12V Power Supply (separate)
        d.move(0, -4)
        d += elm.Label().label('AC 110-240V', loc='left')
        d += elm.Line().right(1)
        d += (psu := elm.Ic(pins=[
            elm.IcPin(name='+12V', side='right', pin='1', anchorname='v12'),
            elm.IcPin(name='GND', side='right', pin='2', anchorname='gnd'),
        ], w=3.0, pinspacing=1.5, label='12V/2A\nPower Supply', lblofst=0))

        # 12V to Hopper
        d.move_from(psu.v12, dx=0.5)
        d += elm.Line().right(1)
        d += elm.Dot()
        v12_node = d.here
        d += elm.Line().right(1.5)
        d += (hopper := elm.Ic(pins=[
            elm.IcPin(name='12V', side='left', pin='1'),
            elm.IcPin(name='GND', side='left', pin='2', anchorname='hgnd'),
            elm.IcPin(name='Motor', side='right', pin='3'),
        ], w=3.5, pinspacing=1.5, label='Azkoyen\nHopper', lblofst=0))

        # Capacitor on 12V line
        d.move_from(v12_node, dx=0, dy=-0.5)
        d += elm.Line().down(0.3)
        d += elm.Capacitor().down().label('2200µF\n25V', loc='right')
        d += elm.Line().down(0.3)
        d += elm.Ground()

        # Hopper GND to common
        d.move_from(hopper.hgnd, dx=-0.5)
        d += elm.Line().left(1)
        d += elm.Dot().label('GND', loc='bottom')
        hopper_gnd = d.here

        # Common GND connection
        d.move_from(gnd_common)
        d += elm.Line().to(hopper_gnd)

        # 12V PSU GND to common
        d.move_from(psu.gnd, dx=0.5)
        d += elm.Line().right(0.5)
        d += elm.Line().to(hopper_gnd)

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
