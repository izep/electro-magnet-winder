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
 * - Synchronized Winding and Traverse (Linear Guide)
 */

#include <Arduino.h>

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

// Configurable constants
const int STEPS_PER_REV = 4096; // 64:1 gear ratio * 64 steps (approx for 28BYJ-48 half-step)
const float TRAVERSE_PITCH_MM = 1.0; // Linear travel per revolution of Traverse motor
const float WIRE_DIAMETER_MM = 0.25; // Typical 30 AWG wire diameter

// State Variables
volatile int currentEncoderPos = 0;
int lastStepPos = 0;
int stepIdx1 = 0;
int stepIdx2 = 0;
bool motorsEngaged = false;
uint32_t lastMoveTime = 0;

// Synchronization logic: Traverse motor moves based on Winding motor position
// steps_traverse = (steps_winding / STEPS_PER_REV) * (WIRE_DIAMETER_MM / TRAVERSE_PITCH_MM) * STEPS_PER_REV
// Simplified: steps_traverse = steps_winding * (WIRE_DIAMETER_MM / TRAVERSE_PITCH_MM)
const float SYNC_RATIO = WIRE_DIAMETER_MM / TRAVERSE_PITCH_MM;

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
    // Display shows turn count (steps / steps per rev)
    int turns = currentEncoderPos / 20; // Scale encoder clicks to turns for display
    refreshDisplay(turns);

    // Check for movement (manual stepping or auto-winding logic)
    if (currentEncoderPos != lastStepPos) {
        int dir = (currentEncoderPos > lastStepPos) ? 1 : -1;
        
        // Move Winding Motor
        stepMotor1(dir);
        
        // Calculate and move Traverse Motor to maintain sync
        // Using currentEncoderPos as a proxy for master step position
        int expectedTraverseSteps = (int)(currentEncoderPos * SYNC_RATIO);
        static int actualTraverseSteps = 0;
        
        while (actualTraverseSteps < expectedTraverseSteps) {
            stepMotor2(1);
            actualTraverseSteps++;
        }
        while (actualTraverseSteps > expectedTraverseSteps) {
            stepMotor2(-1);
            actualTraverseSteps--;
        }

        lastStepPos = currentEncoderPos;
        lastMoveTime = millis();
        motorsEngaged = true;
        delay(2); // Minimum delay between steps for 28BYJ-48
    }

    // Button Reset: Active LOW
    if (digitalRead(ENC_SW) == LOW) {
        currentEncoderPos = 0;
        lastStepPos = 0;
        releaseMotors();
        motorsEngaged = false;
        delay(300); // Debounce
    }
    
    // Release motors after inactivity to prevent overheating
    if (motorsEngaged && (millis() - lastMoveTime > 5000)) {
        releaseMotors();
        motorsEngaged = false;
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
 * Take one step with Motor 1 (Winding) in the specified direction.
 */
void stepMotor1(int dir) {
    stepIdx1 = (stepIdx1 + dir + 8) % 8;
    writeMotor(M1, HALF_STEP[stepIdx1]);
}

/**
 * Take one step with Motor 2 (Traverse) in the specified direction.
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
    static uint32_t lastRefresh = 0;
    
    // Throttle refresh to prevent flickering and allow multiplexing
    if (millis() - lastRefresh < 2) return;
    lastRefresh = millis();

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
    static uint32_t lastInterrupt = 0;
    if (millis() - lastInterrupt < 1) return; // Simple debounce
    
    bool clk = digitalRead(ENC_CLK);
    bool dt  = digitalRead(ENC_DT);
    
    if (clk != dt) {
        currentEncoderPos++;
    } else {
        currentEncoderPos--;
    }
    lastInterrupt = millis();
}
