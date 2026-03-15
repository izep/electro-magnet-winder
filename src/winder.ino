/**
 * Electro-Magnet Winder Firmware
 * Target: Waveshare RP2040-Zero
 * Core:   Earle Philhower RP2040 Arduino core
 */

#include <Arduino.h>
#include <EEPROM.h>

// ── Pin assignments ──────────────────────────────────────────────────────────
const int M1[4]  = {0, 1, 2, 3};             // Winding motor  (ULN2003 #1)
const int M2[4]  = {4, 5, 6, 7};             // Traverse motor (ULN2003 #2)
const int SEG[8] = {8, 9, 10, 11, 12, 13, 14, 15};  // Segments A–G, DP
const int DIG[4] = {16, 17, 18, 19};         // Digit cathodes 0–3 (left→right)

#define ENC_CLK 20
#define ENC_DT  21
#define ENC_SW  22

// ── Motor constants ──────────────────────────────────────────────────────────
const int   STEPS_PER_REV      = 2048;       // 28BYJ-48 half-step
const float GUIDE_STEPS_PER_MM = (float)STEPS_PER_REV / 1.0f; // Simplified for now, should be calibrated
const int   STEP_DELAY_MS      = 2;

// ── AWG wire diameter presets ────────────────────────────────────────────────
struct WireGauge { uint8_t awg; float diameter_mm; };
const WireGauge GAUGES[] = {
  {22, 0.644f}, {24, 0.511f}, {26, 0.405f}, {28, 0.321f}, {30, 0.255f},
  {32, 0.202f}, {34, 0.160f}, {36, 0.127f}, {38, 0.101f}, {40, 0.079f},
};
const int NUM_GAUGES = (int)(sizeof(GAUGES) / sizeof(GAUGES[0]));

// ── Half-step sequence ───────────────────────────────────────────────────────
const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

// ── 7-segment patterns ───────────────────────────────────────────────────────
const uint8_t DIGIT_PAT[10] = {
  0b00111111, 0b00000110, 0b01011011, 0b01001111, 0b01100110,
  0b01101101, 0b01111101, 0b00000111, 0b01111111, 0b01101111,
};

const uint8_t SEG_Y = 0b01101110; // 4-digit common cathode: b, c, d, f, g (Y)

// ── Settings & Persistence ───────────────────────────────────────────────────
struct Settings {
  uint32_t magic;
  int targetLayers;
  int spoolLengthMM;
  int gaugeIndex;
  int motorSpeed;  // Delay in ms (lower is faster)
};
const uint32_t EEPROM_MAGIC = 0x534B4950;

int targetLayers  = 1;
int spoolLengthMM = 20;
int gaugeIndex    = 2;
int motorSpeed    = 2; // Default 2ms

void saveSettings() {
  Settings s = { EEPROM_MAGIC, targetLayers, spoolLengthMM, gaugeIndex, motorSpeed };
  EEPROM.put(0, s);
  EEPROM.commit();
}

void loadSettings() {
  Settings s;
  EEPROM.get(0, s);
  if (s.magic == EEPROM_MAGIC) {
    targetLayers = constrain(s.targetLayers, 1, 99);
    spoolLengthMM = constrain(s.spoolLengthMM, 1, 999);
    gaugeIndex = constrain(s.gaugeIndex, 0, NUM_GAUGES - 1);
    motorSpeed = constrain(s.motorSpeed, 1, 50);
  }
}

// Special characters
const uint8_t SEG_BLANK = 0x00;
const uint8_t SEG_DASH  = 0x40;
const uint8_t SEG_L     = 0x38;
const uint8_t SEG_A_UP  = 0x77;
const uint8_t SEG_S_UP  = 0x6D;
const uint8_t SEG_P_UP  = 0x73;
const uint8_t SEG_G_LO  = 0x6F;
const uint8_t SEG_O_LO  = 0x5C;
const uint8_t SEG_D_LO  = 0x5E;
const uint8_t SEG_N_LO  = 0x54;
const uint8_t SEG_E_LO  = 0x79;
const uint8_t SEG_S_LO  = 0x6D;

// ── State ────────────────────────────────────────────────────────────────────
enum State { MENU_LAYERS, MENU_LENGTH, MENU_GAUGE, MENU_SPEED, MENU_START, WINDING };
State state = MENU_LAYERS;

uint8_t displayBuf[4] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};
int computedTurns = 0;
long totalStepsM1 = 0, totalStepsM2 = 0;
int currentTurns = 0, stepIdx1 = 0, stepIdx2 = 0;
unsigned long lastStepTime = 0;
volatile int encoderDelta = 0;

// ── Handlers ─────────────────────────────────────────────────────────────────
void onEncoderCLK() {
  static uint32_t lastISR = 0;
  uint32_t now = millis();
  if (now - lastISR < 1) return;
  lastISR = now;
  encoderDelta += (digitalRead(ENC_DT) == HIGH) ? 1 : -1;
}

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

void refreshDisplay() {
  static uint8_t digit = 0;
  for (int i = 0; i < 4; i++) digitalWrite(DIG[i], HIGH);
  uint8_t pat = displayBuf[digit];
  for (int i = 0; i < 8; i++) digitalWrite(SEG[i], (pat >> i) & 1);
  digitalWrite(DIG[digit], LOW);
  digit = (digit + 1) % 4;
}

void showNumber(int n) {
  n = constrain(n, 0, 9999);
  displayBuf[0] = DIGIT_PAT[n / 1000];
  displayBuf[1] = DIGIT_PAT[(n / 100) % 10];
  displayBuf[2] = DIGIT_PAT[(n / 10)  % 10];
  displayBuf[3] = DIGIT_PAT[n % 10];
}

void motorStartupTest() {
  for (int i = 0; i < 4; i++) displayBuf[i] = SEG_DASH;
  for (int i = 0; i < 4; i++) { stepMotor1( 1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor1(-1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor2( 1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor2(-1); refreshDisplay(); delay(40); }
  releaseMotors();
}

void updateDisplay() {
  switch (state) {
    case MENU_LAYERS: showNumber(targetLayers); break;
    case MENU_LENGTH: {
      int n = constrain(spoolLengthMM, 0, 999);
      displayBuf[0] = SEG_L;
      displayBuf[1] = (n >= 100) ? DIGIT_PAT[n / 100] : SEG_BLANK;
      displayBuf[2] = (n >= 10) ? DIGIT_PAT[(n / 10) % 10] : SEG_BLANK;
      displayBuf[3] = DIGIT_PAT[n % 10];
      break;
    }
    case MENU_GAUGE:
      displayBuf[0] = SEG_A_UP; displayBuf[1] = SEG_BLANK;
      displayBuf[2] = DIGIT_PAT[GAUGES[gaugeIndex].awg / 10];
      displayBuf[3] = DIGIT_PAT[GAUGES[gaugeIndex].awg % 10];
      break;
    case MENU_SPEED:
      displayBuf[0] = SEG_S_UP; displayBuf[1] = SEG_P_UP;
      displayBuf[2] = (motorSpeed >= 10) ? DIGIT_PAT[motorSpeed / 10] : SEG_BLANK;
      displayBuf[3] = DIGIT_PAT[motorSpeed % 10];
      break;
    case MENU_START:
      displayBuf[0] = SEG_G_LO; displayBuf[1] = SEG_O_LO;
      displayBuf[2] = SEG_BLANK; displayBuf[3] = SEG_BLANK;
      break;
    case WINDING: {
      if (currentTurns < 0) {
        displayBuf[0] = SEG_D_LO; displayBuf[1] = SEG_O_LO;
        displayBuf[2] = SEG_N_LO; displayBuf[3] = SEG_E_LO;
      } else if (currentTurns > 9999) {
        displayBuf[0] = SEG_Y;
        displayBuf[1] = SEG_E_LO;
        displayBuf[2] = SEG_S_LO;
        displayBuf[3] = SEG_BLANK;
      } else {
        showNumber(currentTurns);
      }
      break;
    }
  }
}

void handleEncoder() {
  noInterrupts(); int delta = encoderDelta; encoderDelta = 0; interrupts();
  if (delta == 0) return;
  switch (state) {
    case MENU_LAYERS: targetLayers = constrain(targetLayers + delta, 1, 99); break;
    case MENU_LENGTH: spoolLengthMM = constrain(spoolLengthMM + delta, 1, 999); break;
    case MENU_GAUGE: gaugeIndex = (gaugeIndex + delta + NUM_GAUGES) % NUM_GAUGES; break;
    case MENU_SPEED: motorSpeed = constrain(motorSpeed + delta, 1, 50); break;
    default: break;
  }
}

void handleButton() {
  if (digitalRead(ENC_SW) != LOW) return;
  delay(20); while (digitalRead(ENC_SW) == LOW); delay(20);
  switch (state) {
    case MENU_LAYERS: state = MENU_LENGTH; break;
    case MENU_LENGTH: state = MENU_GAUGE; break;
    case MENU_GAUGE: state = MENU_SPEED; break;
    case MENU_SPEED: state = MENU_START; saveSettings(); break;
    case MENU_START: {
      float wireDiam = GAUGES[gaugeIndex].diameter_mm;
      int turnsPerLayer = max(1, (int)((float)spoolLengthMM / wireDiam));
      computedTurns = targetLayers * turnsPerLayer;
      state = WINDING; currentTurns = 0; totalStepsM1 = 0; totalStepsM2 = 0;
      lastStepTime = millis(); break;
    }
    case WINDING: state = MENU_LAYERS; releaseMotors(); break;
  }
}

void handleWinding() {
  if (currentTurns >= computedTurns) {
    releaseMotors();
    currentTurns = -1; // Flag for display
    unsigned long t = millis();
    while (millis() - t < 3000) { refreshDisplay(); updateDisplay(); }
    state = MENU_LAYERS; return;
  }
  if (digitalRead(ENC_SW) == LOW) { state = MENU_LAYERS; releaseMotors(); delay(500); return; }
  
  unsigned long now = millis();
  if (now - lastStepTime < (unsigned long)motorSpeed) return;
  lastStepTime = now;
  
  stepMotor1(1);
  float wireDiam = GAUGES[gaugeIndex].diameter_mm;
  float revs = (float)totalStepsM1 / STEPS_PER_REV;
  float totalLinearMM = revs * wireDiam;
  
  int passNum = (int)(totalLinearMM / (float)spoolLengthMM);
  float posInPass = fmod(totalLinearMM, (float)spoolLengthMM);
  float guideMM = (passNum % 2 == 0) ? posInPass : (float)spoolLengthMM - posInPass;
  
  long targetStepsM2 = (long)(guideMM * GUIDE_STEPS_PER_MM);
  
  // Basic closed-loop-ish step following
  if (totalStepsM2 < targetStepsM2) stepMotor2(1);
  else if (totalStepsM2 > targetStepsM2) stepMotor2(-1);
  
  currentTurns = (int)revs;
}

void setup() {
  EEPROM.begin(512); loadSettings();
  for (int i = 0; i < 4; i++) { pinMode(M1[i], OUTPUT); digitalWrite(M1[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(M2[i], OUTPUT); digitalWrite(M2[i], LOW); }
  for (int i = 0; i < 8; i++) { pinMode(SEG[i], OUTPUT); digitalWrite(SEG[i], LOW); }
  for (int i = 0; i < 4; i++) { pinMode(DIG[i], OUTPUT); digitalWrite(DIG[i], HIGH); }
  pinMode(ENC_CLK, INPUT_PULLUP); pinMode(ENC_DT, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, FALLING);
  motorStartupTest(); updateDisplay();
}

void loop() {
  refreshDisplay(); handleButton();
  if (state == WINDING) handleWinding();
  else handleEncoder();
  updateDisplay();
}
