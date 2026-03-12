# Electro-Magnet Winder - Requirements

## Overview
A DIY machine for winding precision electro-magnets and coils, powered by an RP2040-Zero microcontroller.

## Hardware Specifications
- **Controller**: Waveshare RP2040-Zero
- **Actuators**: 
  - 2x 28BYJ-48 Stepper Motors (64:1 gear ratio)
  - 2x ULN2003 Driver Boards
- **Input**: 
  - KY-040 Rotary Encoder (Navigation and count adjustment)
- **Output**: 
  - 4-Digit 7-Segment Display (SH5461AS, common cathode) for turn count/status.

## Functional Requirements

1. **Menu-driven configuration**: Set target turns, spool/magnet length, and wire gauge before winding.
2. **Automatic Winding**: Press start; machine winds to target turn count and stops automatically.
3. **Dual Motor Sync**:
   - Motor 1: Winding (rotation)
   - Motor 2: Wire guide (linear traverse, ping-pong across spool length)
4. **Display**: Real-time turn count while winding; settings shown in menu.
5. **Wire gauge presets**: 22, 26, 28, 30, 32, 36, 40 AWG — selectable at runtime.
6. **Startup test**: Brief motor movement on power-up to verify coil wiring.
7. **Memory**: Persistence of settings (deferred to future version).

## Technical Stack
- **Firmware**: C++/Arduino (Earle Philhower RP2040 core)
- **CAD**: STL models for 3D printed enclosure
- **Documentation**: Markdown-based wiring and assembly guides

## Roadmap

- [x] Initial wiring and pinout defined
- [x] Basic motor/display test sketch
- [x] Implement traverse/winding sync logic (ping-pong guide)
- [x] Menu system: target turns, spool length, wire gauge, start/stop
- [x] Wire gauge presets (22–40 AWG, selectable at runtime)
- [x] Startup motor test sequence
- [x] 3D print and assemble enclosure
- [x] Persistence of settings (implemented via EEPROM)
