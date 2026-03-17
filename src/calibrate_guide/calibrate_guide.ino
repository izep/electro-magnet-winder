#include <Arduino.h>

const int M2[4] = {4, 5, 6, 7}; // Guide Motor Pins (GP4-7)
// High-Torque Full-Step Sequence (2-phase on)
const uint8_t FULL_STEP[4] = {0x3, 0x6, 0xC, 0x9}; // 0011, 0110, 1100, 1001

void writeMotor(const int pins[4], uint8_t nibble) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(pins[i], (nibble >> i) & 1);
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) {
    pinMode(M2[i], OUTPUT);
    digitalWrite(M2[i], LOW);
  }
  
  delay(3000); // Wait for user
  Serial.println("Starting 30-revolution MAX TORQUE move (CCW)...");

  // Based on your 2048 steps/rev (Half-step) in the main winder, 
  // Full-step mode uses 1024 steps per revolution.
  long totalSteps = 1024L * 30L; 
  int stepIdx = 0;

  for (long i = 0; i < totalSteps; i++) {
    // CCW move (decrementing index)
    stepIdx = (stepIdx - 1 + 4) % 4;
    writeMotor(M2, FULL_STEP[stepIdx]);
    
    // Slowed down to 4ms for maximum pull-in torque
    delay(4); 
    
    if (i % 1024 == 0 && i > 0) {
      Serial.print("Completed rev: ");
      Serial.println(i / 1024);
    }
  }

  // Release all coils
  for (int i = 0; i < 4; i++) digitalWrite(M2[i], LOW);
  Serial.println("Calibration move FINISHED.");
  Serial.println("Measure the total MM traveled for these 30 revolutions.");
}

void loop() {}
