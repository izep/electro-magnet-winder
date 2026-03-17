#include "hardware/gpio.h"
/**
 * Electro-Magnet Winder Firmware
 * Target: Waveshare RP2040-Zero
 * Core:   Earle Philhower RP2040 Arduino core
 */

#include <Arduino.h>
#include <EEPROM.h>
#include "EEPROM_Settings.h"

SettingsManager settingsManager;

// ── Pin assignments ──────────────────────────────────────────────────────────

const int M1[4]  = {0, 1, 2, 3};             // Winding motor  (ULN2003 #1)
const int M2[4]  = {4, 5, 6, 7};             // Traverse motor (ULN2003 #2)
const int SEG[7] = {8, 27, 10, 11, 12, 15, 14};  // Segments A, B, C, D, E, F, G
const int DIG[4] = {9, 13, 26, 28};             // Digit cathodes 1, 2, 3, 4

#define ENC_CLK  0
#define ENC_DT   1
#define ENC_SW  29

// ── Motor constants ──────────────────────────────────────────────────────────

const int   STEPS_PER_REV      = 4096;       // 28BYJ-48 half-step (64:1 gear)
const float GUIDE_STEPS_PER_MM = 1575.385f;  // From calibration (61440 steps / 39mm)
const int   STEP_DELAY_MS      = 2;          // ms between winding motor steps

// ── AWG wire diameter presets ────────────────────────────────────────────────

struct WireGauge { uint8_t awg; float diameter_mm; };

const WireGauge GAUGES[] = {
  {22, 0.644f}, {26, 0.405f}, {28, 0.321f}, {30, 0.255f},
  {32, 0.202f}, {36, 0.127f}, {40, 0.079f},
};
const int NUM_GAUGES = (int)(sizeof(GAUGES) / sizeof(GAUGES[0]));

// ── Half-step sequence ───────────────────────────────────────────────────────

const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

// ── 7-segment patterns ───────────────────────────────────────────────────────

const uint8_t DIGIT_PAT[10] = {
  0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
  0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111,
};

const uint8_t SEG_BLANK = 0x00;
const uint8_t SEG_DASH  = 0x40;
const uint8_t SEG_L     = 0x38;
const uint8_t SEG_A_UP  = 0x77;
const uint8_t SEG_G_LO  = 0x6F;
const uint8_t SEG_O_LO  = 0x5C;
const uint8_t SEG_D_LO  = 0x5E;
const uint8_t SEG_N_LO  = 0x54;
const uint8_t SEG_E_LO  = 0x79;
const uint8_t SEG_H     = 0x76;
const uint8_t SEG_C_UP  = 0x39;

// ── State & Defaults ─────────────────────────────────────────────────────────

enum State { MENU_LAYERS, MENU_LENGTH, MENU_GAUGE, MENU_DIR, MENU_HOME, MENU_START, WINDING };
State state = MENU_LAYERS;

int targetLayers  = 10;   // Default: 10 layers
int spoolLengthMM = 50;   // Default: 50 mm
int gaugeIndex    = 0;    // Default: 22 AWG
int guideDir      = -1;   // Default: CC (-1)

uint8_t displayBuf[4] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};

// ── Winding state ────────────────────────────────────────────────────────────

int           computedTurns = 0;
long          totalStepsM1 = 0;
long          totalStepsM2 = 0;
int           currentTurns = 0;
int           stepIdx1     = 0;
int           stepIdx2     = 0;
unsigned long lastStepTime = 0;

volatile int encoderDelta = 0;

void onEncoderCLK() {
  static uint32_t lastISR = 0;
  uint32_t now = millis();
  if (now - lastISR < 1) return;
  lastISR = now;
  encoderDelta += (digitalRead(ENC_DT) == HIGH) ? -1 : 1;
}

// ── Motor helpers ────────────────────────────────────────────────────────────

void writeMotor(const int pins[4], uint8_t nibble) {
  for (int i = 0; i < 4; i++) digitalWrite(pins[i], (nibble >> i) & 1);
}

void stepMotor1(int dir) {
  stepIdx1 = (stepIdx1 + dir + 8) % 8;
  writeMotor(M1, HALF_STEP[stepIdx1]);
  totalStepsM1 += dir;
}

void stepMotor2(int dir) {
  stepIdx2 = (stepIdx2 + dir + 8) % 8;
  writeMotor(M2, HALF_STEP[stepIdx2]);
  totalStepsM2 += dir;
}

void releaseMotors() { writeMotor(M1, 0); writeMotor(M2, 0); }

void enterMenuMode() {
  writeMotor(M1, 0);
  detachInterrupt(digitalPinToInterrupt(ENC_CLK));
  pinMode(ENC_CLK, INPUT_PULLDOWN);
  pinMode(ENC_DT,  INPUT_PULLDOWN);
  encoderDelta = 0;
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, FALLING);
}

void enterWindingMode() {
  detachInterrupt(digitalPinToInterrupt(ENC_CLK));
  pinMode(ENC_CLK, OUTPUT); digitalWrite(ENC_CLK, LOW);
  pinMode(ENC_DT,  OUTPUT); digitalWrite(ENC_DT,  LOW);
}

// ── Display helpers ──────────────────────────────────────────────────────────

void refreshDisplay() {
  static uint8_t digit = 0;
  for (int i = 0; i < 4; i++) digitalWrite(DIG[i], HIGH);
  uint8_t pat = displayBuf[digit];
  for (int i = 0; i < 7; i++) digitalWrite(SEG[i], (pat >> i) & 1);
  digitalWrite(DIG[digit], LOW);
  digit = (digit + 1) % 4;
}

void showNumber(int n) {
  n = constrain(n, 0, 9999);
  displayBuf[0] = (n >= 1000) ? DIGIT_PAT[n / 1000] : SEG_BLANK;
  displayBuf[1] = (n >= 100)  ? DIGIT_PAT[(n / 100) % 10] : SEG_BLANK;
  displayBuf[2] = (n >= 10)   ? DIGIT_PAT[(n / 10)  % 10] : SEG_BLANK;
  displayBuf[3] = DIGIT_PAT[n % 10];
}

void motorStartupTest() {
  for (int i = 0; i < 4; i++) displayBuf[i] = SEG_DASH;
  for (int i = 0; i < 400; i++) { stepMotor1( 1); refreshDisplay(); delay(2); }
  for (int i = 0; i < 400; i++) { stepMotor1(-1); refreshDisplay(); delay(2); }
  for (int i = 0; i < 400; i++) { stepMotor2( 1); refreshDisplay(); delay(2); }
  for (int i = 0; i < 400; i++) { stepMotor2(-1); refreshDisplay(); delay(2); }
  releaseMotors();
}

void updateDisplay() {
  switch (state) {
    case MENU_LAYERS: showNumber(targetLayers); break;
    case MENU_LENGTH: {
      int n = constrain(spoolLengthMM, 0, 999);
      displayBuf[0] = SEG_L;
      displayBuf[1] = (n >= 100) ? DIGIT_PAT[n / 100] : SEG_BLANK;
      displayBuf[2] = (n >= 10)  ? DIGIT_PAT[(n / 10) % 10] : SEG_BLANK;
      displayBuf[3] = DIGIT_PAT[n % 10];
      break;
    }
    case MENU_GAUGE:
      displayBuf[0] = SEG_A_UP; displayBuf[1] = SEG_BLANK;
      displayBuf[2] = DIGIT_PAT[GAUGES[gaugeIndex].awg / 10];
      displayBuf[3] = DIGIT_PAT[GAUGES[gaugeIndex].awg % 10];
      break;
    case MENU_DIR:
      displayBuf[0] = SEG_D_LO; displayBuf[1] = SEG_BLANK;
      displayBuf[2] = SEG_C_UP; 
      displayBuf[3] = (guideDir == 1) ? SEG_BLANK : SEG_C_UP; // "d  C" or "d CC"
      break;
    case MENU_HOME:
      displayBuf[0] = SEG_H; displayBuf[1] = SEG_O_LO;
      displayBuf[2] = SEG_N_LO; displayBuf[3] = SEG_E_LO;
      break;
    case MENU_START:
      displayBuf[0] = SEG_G_LO; displayBuf[1] = SEG_O_LO;
      displayBuf[2] = SEG_BLANK; displayBuf[3] = SEG_BLANK;
      break;
    case WINDING: showNumber(currentTurns); break;
  }
}

void handleEncoder() {
  noInterrupts(); int delta = encoderDelta; encoderDelta = 0; interrupts();
  if (delta == 0) return;
  bool changed = false;
  switch (state) {
    case MENU_LAYERS: targetLayers = constrain(targetLayers + delta, 1, 99); settingsManager.current.targetLayers = targetLayers; changed = true; break;
    case MENU_LENGTH: spoolLengthMM = constrain(spoolLengthMM + delta, 1, 999); settingsManager.current.spoolLengthMM = spoolLengthMM; changed = true; break;
    case MENU_GAUGE:  gaugeIndex = (gaugeIndex + delta + NUM_GAUGES) % NUM_GAUGES; settingsManager.current.gaugeIndex = gaugeIndex; changed = true; break;
    case MENU_DIR:    if (delta != 0) { guideDir *= -1; settingsManager.current.guideDir = guideDir; changed = true; } break;
    case MENU_HOME: {
      // 1 Encoder rotation (~20 clicks) = 1 Motor rotation (4096 steps)
      int steps = delta * 205; 
      int dir = (steps > 0) ? 1 : -1;
      for (int i = 0; i < abs(steps); i++) { stepMotor2(dir); delay(2); if (i % 4 == 0) refreshDisplay(); }
      releaseMotors(); break;
    }
    default: break;
  }
  if (changed) settingsManager.save();
}

void handleButton() {
  if (digitalRead(ENC_SW) != LOW) return;
  delay(20); while (digitalRead(ENC_SW) == LOW); delay(20);
  switch (state) {
    case MENU_LAYERS: state = MENU_LENGTH; break;
    case MENU_LENGTH: state = MENU_GAUGE;  break;
    case MENU_GAUGE:  state = MENU_DIR;    break;
    case MENU_DIR:    state = MENU_HOME;   break;
    case MENU_HOME:   state = MENU_START;  break;
    case MENU_START: {
      float wireDiam = GAUGES[gaugeIndex].diameter_mm;
      int turnsPerLayer = max(1, (int)((float)spoolLengthMM / wireDiam));
      computedTurns = targetLayers * turnsPerLayer;
      enterWindingMode();
      state = WINDING; currentTurns = 0; totalStepsM1 = 0; totalStepsM2 = 0;
      lastStepTime = millis(); break;
    }
    case WINDING: releaseMotors(); enterMenuMode(); state = MENU_HOME; break;
  }
}

void handleWinding() {
  if (currentTurns >= computedTurns) {
    releaseMotors();
    displayBuf[0] = SEG_D_LO; displayBuf[1] = SEG_O_LO; displayBuf[2] = SEG_N_LO; displayBuf[3] = SEG_E_LO;
    unsigned long t = millis(); while (millis() - t < 2000) refreshDisplay();
    enterMenuMode(); state = MENU_LAYERS; return;
  }
  if (millis() - lastStepTime < (unsigned long)STEP_DELAY_MS) return;
  lastStepTime = millis();

  stepMotor1(1); // Spindle Clockwise

  float wireDiam = GAUGES[gaugeIndex].diameter_mm;
  float passLength = (float)spoolLengthMM;
  float totalRevs = (float)totalStepsM1 / STEPS_PER_REV;
  float targetMM = totalRevs * wireDiam;
  
  int passNum = (int)(targetMM / passLength);
  float posInPass = fmod(targetMM, passLength);
  
  // Logic: guideMM always starts at 0 relative to Hone. 
  // Physical direction is controlled by multiplying by guideDir.
  float guideMM = (passNum % 2 == 0) ? posInPass : passLength - posInPass;
  
  long targetStepsM2 = (long)(guideMM * GUIDE_STEPS_PER_MM * (float)guideDir);

  if (totalStepsM2 < targetStepsM2) stepMotor2(1); 
  else if (totalStepsM2 > targetStepsM2) stepMotor2(-1);

  currentTurns = (int)(totalStepsM1 / STEPS_PER_REV);
}

void setup() {
  settingsManager.begin();
  targetLayers = settingsManager.current.targetLayers;
  spoolLengthMM = settingsManager.current.spoolLengthMM;
  gaugeIndex = settingsManager.current.gaugeIndex;
  guideDir = settingsManager.current.guideDir;
  for (int i = 0; i < 4; i++) { pinMode(M1[i], OUTPUT); digitalWrite(M1[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(M2[i], OUTPUT); digitalWrite(M2[i], LOW); }
  for (int i = 0; i < 7; i++) { pinMode(SEG[i], OUTPUT); digitalWrite(SEG[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(DIG[i], OUTPUT); digitalWrite(DIG[i], HIGH); gpio_set_drive_strength((uint)DIG[i], GPIO_DRIVE_STRENGTH_12MA); }
  pinMode(ENC_SW, INPUT_PULLUP);
  motorStartupTest(); enterMenuMode(); updateDisplay();
}

void loop() { refreshDisplay(); handleButton(); if (state == WINDING) handleWinding(); else handleEncoder(); updateDisplay(); }
