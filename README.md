# Electro-Magnet Winder

A DIY machine for winding precision electro-magnets and coils, powered by an RP2040-Zero microcontroller.

## Project Structure

| Path | Contents |
|------|----------|
| `src/winder.ino` | Firmware (single unified sketch) |
| `wiring/` | Wiring diagrams and pinout references |
| `model/` | 3D-printable STL files for the enclosure |

## Hardware Components

- **Microcontroller**: RP2040-Zero (Waveshare/clone)
- **Motor 1 (winding)**: 28BYJ-48 + ULN2003 driver board
- **Motor 2 (traverse/guide)**: 28BYJ-48 + ULN2003 driver board
- **Display**: SH5461AS 4-digit 7-segment (common cathode)
- **Input**: KY-040 rotary encoder

See `wiring/rp2040-zero-dual-stepper.md` for the full pinout and wiring diagram.

## Getting Started

1. Follow the wiring diagram in `wiring/rp2040-zero-dual-stepper.md`.
2. Install the **Earle Philhower RP2040 Arduino core** in the Arduino IDE.
3. Open `src/winder.ino` and upload to the RP2040-Zero.

## Using the Firmware

On power-up the machine runs a brief motor test (display shows `----`) to confirm both motors are wired correctly. Both motors step forward 4 steps then back 4 steps before the menu appears.

### Menu flow

The **encoder button** advances through each setting. Rotate the encoder to change the current value.

| Display | State | Encoder action |
|---------|-------|---------------|
| `0100`  | Target turns | Rotate to set turn count (±10, range 10–9990) |
| `L 20`  | Spool length | Rotate to set magnet/coil length in mm (±1 mm, range 1–999) |
| `A 28`  | Wire gauge | Rotate to cycle AWG presets: 22, 26, 28, 30, 32, 36, 40 |
| `go`    | Ready | Press button to start winding |
| `0000`  | **Winding** | Display counts up to target turns — press button to stop early |

When winding completes (target reached) or is stopped, the machine returns to the turn count setting screen.

### Guide motor calibration

2 revolutions of Motor 2 = 3.5 mm of linear travel. This is fixed in firmware as `GUIDE_STEPS_PER_MM = (2 × 2048) / 3.5 ≈ 1170`. Adjust this constant in `winder.ino` if your lead screw or drive mechanism differs.
