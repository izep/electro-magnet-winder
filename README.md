# Electro-Magnet Winder

A DIY machine for winding precision electro-magnets and coils, powered by an RP2040-Zero microcontroller.

## Project Structure
- `model/`: 3D-printable STL files for the enclosure.
- `wiring/`: Wiring diagrams and pinout references.
- `src/`: Arduino/C++ source code for the firmware.

## Hardware Components
- **Microcontroller**: RP2040-Zero (Waveshare/clone)
- **Motors**: 2x 28BYJ-48 Stepper Motors (with ULN2003 drivers)
- **Display**: SH5461AS 4-Digit 7-Segment Display (Common Cathode)
- **Input**: KY-040 Rotary Encoder

## Getting Started
1. Follow the wiring diagram in `wiring/rp2040-zero-dual-stepper.md`.
2. Install the **Earle Philhower RP2040 Arduino core** in the Arduino IDE.
3. Open and upload the sketch from `src/`.
