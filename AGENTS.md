# GEMINI.md - Electro-Magnet Winder Project Context

## Project Overview
The **Electro-Magnet Winder** is a DIY machine powered by an **RP2040-Zero** (Waveshare) for precision winding of electro-magnets and coils. It uses a dual-stepper motor system to synchronize spindle rotation with a linear traverse wire guide.

### Key Technologies
- **Microcontroller**: RP2040-Zero (C++/Arduino with Earle Philhower RP2040 core).
- **Actuators**: 2x 28BYJ-48 Stepper Motors (64:1 gear ratio) + ULN2003 drivers.
- **Display**: SH5461AS 4-digit 7-segment (Common Cathode), logic remapped for 20-pin RP2040-Zero header compatibility.
- **Input**: KY-040 Rotary Encoder (shared pins with Motor 1 during menu navigation).

## Directory Structure
- `src/winder/winder.ino`: Main firmware sketch.
- `src/display_diagnostic/`: Diagnostic tool for verifying SH5461AS wiring.
- `src/calibrate_guide/`: Tool for measuring linear travel per motor revolution.
- `wiring/`: Mermaid diagrams and detailed pinout documentation.
- `model/`: 3D-printable STL files for the enclosure.

## Building and Running
### Requirements
- **Arduino CLI** or Arduino IDE.
- **Earle Philhower RP2040 Core** (`rp2040:rp2040`).
- **Board FQBN**: `rp2040:rp2040:waveshare_rp2040_zero`.

### Commands
- **Compile**: `arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero src/winder/winder.ino`
- **Upload**: Put the RP2040 into BOOTSEL mode and copy the generated `.uf2` file to the `RPI-RP2` volume.

## Hardware Configuration & Calibration
### Pin Sharing
GP0 and GP1 are shared between Motor 1 (Winding) and the Encoder (Navigation). 
- **Menu Mode**: Pins are `INPUT_PULLDOWN`.
- **Winding Mode**: Pins are `OUTPUT`.

### Calibration Constants
- **STEPS_PER_REV**: 4096 (standard half-step for 28BYJ-48).
- **GUIDE_STEPS_PER_MM**: 1575.385 (Calibrated at 30 revs = 39 mm).

## Development Conventions
- **Direct GPIO**: Use `digitalWrite` and `pinMode` for motor and display control; avoid heavy libraries to stay within RP2040-Zero's limited pin count.
- **Multiplexing**: The display is multiplexed manually in the `loop()` via `refreshDisplay()`.
- **Phase Boundary**: Always call `enterMenuMode()` or `enterWindingMode()` when transitioning states to handle GP0/GP1 reconfiguration.
- **Safety**: `GP29` (Encoder Switch) is a dedicated emergency stop that works in both menu and winding phases.
