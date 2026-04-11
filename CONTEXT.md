# TinyTach Project Context

## Hardware
- ATtiny85 microcontroller
- WS2812B 8-LED bar (56mm x 10mm)
- USBasp programmer
- XR650L motorcycle tachometer project

## Wiring
- LED data: PB0 (pin 5) via 300Ω resistor
- LED VCC: 5V rail
- LED GND: GND rail
- Tach signal input: PB2 (pin 7) via voltage divider (10kΩ/4.7kΩ) from Ignitech 12V tach output

## Ignitech tach output
- 12V signal
- 1 pulse per revolution
- RPM = 60,000,000 / pulse_interval_microseconds

## Toolchain
- arduino-cli
- ATTinyCore:avr:attinyx5:chip=85,clock=8internal
- Library: tinyNeoPixel_Static
- Upload: make (USBasp programmer)

## LED plan
- 8 LEDs = 1 per 1000 RPM
- Colors: green (low) -> yellow (mid) -> red (high/redline)
- XR650L redline ~7500 RPM
