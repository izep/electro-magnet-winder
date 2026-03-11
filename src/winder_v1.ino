#include <Arduino.h>

/**
 * Electro-Magnet Winder Firmware v0.2
 * 
 * Target: RP2040-Zero (Waveshare/clone)
 * Core: Earle Philhower RP2040 Arduino core
 * 
 * Hardware:
 * - 2x 28BYJ-48 Stepper Motors (ULN2003 Drivers)
 * - 1x SH5461AS 4-Digit 7-Segment Display (Common Cathode)
 * - 1x KY-040 Rotary Encoder
 * 
 * Features:
 * - Real-time turn count display
 * - Manual stepping with rotary encoder
 * - Synchronized Traverse/Winding logic
 */

// Motor 1: Winding (Rotation)
const int M1[4] = {0, 1, 2, 3};
// Motor 2: Traverse (Wire Guide)
const int M2[4] = {4, 5, 6, 7};

// Display segment pins (A, B, C, D, E, F, G, DP)
// Connected via 150 Ohm resistors to GP8-15
const int SEG[8] = {8, 9, 10, 11, 12, 13, 14, 15};

// Display digit common cathode pins (Digit 1-4)
const int DIG[4] = {16, 17, 18, 19};

// KY-040 rotary encoder
#define ENC_CLK 20
#define ENC_DT  21
#define ENC_SW  22

// Half-step sequence for 28BYJ-48 (IN1 is LSB)
const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

// Configuration Constants
const int STEPS_PER_REV = 4096; // 64 steps * 64 gear ratio for 28BYJ-48 half-stepping
const float WIRE_DIAMETER_MM = 0.2; // Example for 32 AWG
const float TRAVERSE_MM_PER_REV = 0.5; // Lead screw pitch (example: 0.5mm)
const int TRAVERSE_STEPS_PER_MM = 200; // Example: 200 steps per mm for M3/M4 screw

// Calculated Traverse steps per Winding step
// Ratio = (Traverse_steps_per_mm * Wire_dia_mm) / Steps_per_rev
const float TRAVERSE_RATIO = (float)(TRAVERSE_STEPS_PER_MM * WIRE_DIAMETER_MM) / STEPS_PER_REV;

// 7-segment digit patterns (Common Cathode, Segments A-G, DP=bit 7)
// Bit 0 = A, 1 = B, 2 = C, 3 = D, 4 = E, 5 = F, 6 = G, 7 = DP
const uint8_t DIGIT_PAT[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9
};

// State Variables
volatile int windingPosition = 0; // Total half-steps
volatile float traversePosition = 0; // Target traverse position in steps
int lastWindingPos = 0;
int stepIdx1 = 0;
int stepIdx2 = 0;
bool motorsEngaged = false;

// Function Prototypes
void writeMotor(const int pins[4], uint8_t nibble);
void stepMotor1(int dir);
void stepMotor2(int dir);
void releaseMotors();
void refreshDisplay(int value);
void onEncoderCLK();

void setup() {
    // Configure Motor Pins
    for (int i = 0; i < 4; i++) {
        pinMode(M1[i], OUTPUT);
        digitalWrite(M1[i], LOW);
        pinMode(M2[i], OUTPUT);
        digitalWrite(M2[i], LOW);
    }

    // Configure Display Pins
    for (int i = 0; i < 8; i++) {
        pinMode(SEG[i], OUTPUT);
        digitalWrite(SEG[i], LOW);
    }
    for (int i = 0; i < 4; i++) {
        pinMode(DIG[i], OUTPUT);
        digitalWrite(DIG[i], HIGH); // Off for common cathode
    }

    // Configure Encoder Pins
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT,  INPUT_PULLUP);
    pinMode(ENC_SW,  INPUT_PULLUP);
    
    // Attach interrupt for encoder CLK
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, CHANGE);
}

void loop() {
    // Refresh the 4-digit display (cycles one digit per call)
    refreshDisplay(windingPosition / STEPS_PER_REV);

    // Check for encoder movement (manual stepping)
    if (windingPosition != lastWindingPos) {
        int dir = (windingPosition > lastWindingPos) ? 1 : -1;
        
        // 1. Step Winding Motor
        stepMotor1(dir);
        
        // 2. Calculate and Step Traverse Motor
        float targetTraverse = windingPosition * TRAVERSE_RATIO;
        int traverseDiff = (int)targetTraverse - (int)traversePosition;
        
        if (traverseDiff != 0) {
            stepMotor2(traverseDiff > 0 ? 1 : -1);
            traversePosition += (traverseDiff > 0 ? 1 : -1);
        }

        lastWindingPos = windingPosition;
        motorsEngaged = true;
        delay(2); // Minimum delay between steps for 28BYJ-48
    }

    // Button Reset: Active LOW
    if (digitalRead(ENC_SW) == LOW) {
        windingPosition = 0;
        lastWindingPos = 0;
        traversePosition = 0;
        releaseMotors();
        motorsEngaged = false;
        delay(300); // Debounce
    }
    
    // Release motors after inactivity to prevent overheating
    static uint32_t lastMoveTime = 0;
    if (motorsEngaged && (millis() - lastMoveTime > 5000)) {
        releaseMotors();
        motorsEngaged = false;
    }
    if (windingPosition != lastWindingPos) {
        lastMoveTime = millis();
    }
}

/**
 * Write a 4-bit nibble to the motor driver pins.
 */
void writeMotor(const int pins[4], uint8_t nibble) {
    for (int i = 0; i < 4; i++) {
        digitalWrite(pins[i], (nibble >> i) & 1);
    }
}

/**
 * Take one step with Motor 1 in the specified direction.
 */
void stepMotor1(int dir) {
    stepIdx1 = (stepIdx1 + dir + 8) % 8;
    writeMotor(M1, HALF_STEP[stepIdx1]);
}

/**
 * Take one step with Motor 2 in the specified direction.
 */
void stepMotor2(int dir) {
    stepIdx2 = (stepIdx2 + dir + 8) % 8;
    writeMotor(M2, HALF_STEP[stepIdx2]);
}

/**
 * Cut power to all motor coils.
 */
void releaseMotors() {
    writeMotor(M1, 0);
    writeMotor(M2, 0);
}

/**
 * Refresh the display. Advances one digit per call.
 * Multiplexed display logic for 4-digit 7-segment.
 */
void refreshDisplay(int value) {
    static uint8_t currentDigit = 0;
    int absValue = abs(value) % 10000;

    int digits[4] = {
        absValue / 1000,
        (absValue / 100) % 10,
        (absValue / 10)  % 10,
        absValue         % 10,
    };

    // 1. Turn off all digits (HIGH for common cathode)
    for (int i = 0; i < 4; i++) {
        digitalWrite(DIG[i], HIGH);
    }

    // 2. Set segment pins for the current digit
    uint8_t pattern = DIGIT_PAT[digits[currentDigit]];
    for (int i = 0; i < 8; i++) {
        digitalWrite(SEG[i], (pattern >> i) & 1);
    }

    // 3. Enable the current digit cathode (LOW)
    digitalWrite(DIG[currentDigit], LOW);

    // 4. Advance to next digit for next call
    currentDigit = (currentDigit + 1) % 4;
}

/**
 * Interrupt handler for rotary encoder rotation.
 */
void onEncoderCLK() {
    bool clk = digitalRead(ENC_CLK);
    bool dt  = digitalRead(ENC_DT);
    
    if (clk != dt) {
        windingPosition++;
    } else {
        windingPosition--;
    }
}
