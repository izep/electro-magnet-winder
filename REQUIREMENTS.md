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
1. **Manual Mode**: Use rotary encoder to manually step the main winding motor.
2. **Automatic Winding**: Set a target number of turns and start/stop winding.
3. **Dual Motor Sync**: 
   - Motor 1: Winding (Rotation)
   - Motor 2: Wire Guide (Linear traverse to ensure even layers)
4. **Display**: Real-time turn count display.
5. **Memory**: Persistence of turn count and settings (optional, for later).

## Technical Stack
- **Firmware**: C++/Arduino (Earle Philhower RP2040 core)
- **CAD**: STL models for 3D printed enclosure
- **Documentation**: Markdown-based wiring and assembly guides

## Roadmap
- [x] Initial wiring and pinout defined
- [x] Basic motor/display test sketch
- [ ] Implement Traverse/Winding sync logic
- [ ] Add menu system for target turn setting
- [ ] 3D Print and assemble enclosure
