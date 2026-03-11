#include <Arduino.h>

// Motor 1 (Core) pins: GP0-GP3
const int M1[4] = {0, 1, 2, 3};
// Motor 2 (Guide) pins: GP4-GP7
const int M2[4] = {4, 5, 6, 7};

// Display segment pins: GP8-GP15 (A, B, C, D, E, F, G, DP)
const int SEG[8] = {8, 9, 10, 11, 12, 13, 14, 15};
// Display digit cathode pins: GP16-GP19 (Digit 1-4)
const int DIG[4] = {16, 17, 18, 19};

// KY-040 rotary encoder pins: GP20-GP22
#define ENC_CLK 20
#define ENC_DT  21
#define ENC_SW  22

// Half-step sequence (IN1 = LSB)
const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

// 7-segment digit patterns (common cathode, segments A-G)
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

// State variables
volatile int targetTurns = 100;
int currentTurns = 0;
bool isWinding = false;
int stepIdx1 = 0;
int stepIdx2 = 0;
unsigned long lastStepTime = 0;
const int stepDelay = 2; // ms

void writeMotor(const int pins[4], uint8_t nibble) {
  for (int i = 0; i < 4; i++)
    digitalWrite(pins[i], (nibble >> i) & 1);
}

void stepMotor1(int dir) {
  stepIdx1 = (stepIdx1 + dir + 8) % 8;
  writeMotor(M1, HALF_STEP[stepIdx1]);
}

void stepMotor2(int dir) {
  stepIdx2 = (stepIdx2 + dir + 8) % 8;
  writeMotor(M2, HALF_STEP[stepIdx2]);
}

void refreshDisplay(int n) {
  static uint8_t currentDigit = 0;
  n = constrain(n, 0, 9999);

  int displayDigits[4] = {
    n / 1000,
    (n / 100) % 10,
    (n / 10)  % 10,
    n         % 10,
  };

  for (int i = 0; i < 4; i++) digitalWrite(DIG[i], HIGH);

  uint8_t pat = DIGIT_PAT[displayDigits[currentDigit]];
  for (int i = 0; i < 8; i++)
    digitalWrite(SEG[i], (pat >> i) & 1);

  digitalWrite(DIG[currentDigit], LOW);
  currentDigit = (currentDigit + 1) % 4;
}

void onEncoder() {
  if (!isWinding) {
    targetTurns += (digitalRead(ENC_DT) != digitalRead(ENC_CLK)) ? 10 : -10;
    targetTurns = constrain(targetTurns, 0, 9990);
  }
}

void setup() {
  for (int i = 0; i < 4; i++) { pinMode(M1[i], OUTPUT); digitalWrite(M1[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(M2[i], OUTPUT); digitalWrite(M2[i], LOW); }
  for (int i = 0; i < 8; i++) { pinMode(SEG[i], OUTPUT); digitalWrite(SEG[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(DIG[i], OUTPUT); digitalWrite(DIG[i], HIGH); }

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoder, CHANGE);
}

void loop() {
  refreshDisplay(isWinding ? currentTurns : targetTurns);

  if (digitalRead(ENC_SW) == LOW) {
    delay(200); // debounce
    isWinding = !isWinding;
    if (isWinding) currentTurns = 0;
  }

  if (isWinding && currentTurns < targetTurns) {
    if (millis() - lastStepTime >= stepDelay) {
      stepMotor1(1);
      // Simple linear guide logic could go here
      // For now, just count steps to turns (2048 steps/turn)
      static int steps = 0;
      steps++;
      if (steps >= 2048) {
        steps = 0;
        currentTurns++;
      }
      lastStepTime = millis();
    }
  } else if (currentTurns >= targetTurns) {
    isWinding = false;
    writeMotor(M1, 0);
    writeMotor(M2, 0);
  }
}
