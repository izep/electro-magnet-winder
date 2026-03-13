/**
 * Electro-Magnet Winder Firmware
 * Target: Waveshare RP2040-Zero
 * Core:   Earle Philhower RP2040 Arduino core
 *
 * Hardware:
 *   - Motor 1  (winding):  28BYJ-48 + ULN2003 on GP0–GP3
 *   - Motor 2  (traverse): 28BYJ-48 + ULN2003 on GP4–GP7
 *   - Display:             SH5461AS 4-digit 7-seg (common cathode) on GP8–GP19
 *   - Encoder:             KY-040 on GP20–GP22
 *
 * Menu flow (button advances each step):
 *   LAYERS → LENGTH → GAUGE → START  →  WINDING
 *      ↑__________________________________|  (stop returns here)
 *
 *   LAYERS – encoder sets number of winding layers (±1, range 1–99)
 *   LENGTH – encoder sets magnet/spool length in mm (±1 mm, range 1–999)
 *   GAUGE  – encoder cycles AWG presets (22–40 AWG)
 *   START  – display shows "go", button begins winding
 *   WINDING– display shows live turn count; button stops
 *
 * Total turns are computed automatically:
 *   turns_per_layer = floor(spool_length_mm / wire_diameter_mm)
 *   total_turns     = layers × turns_per_layer
 *
 * Guide motor calibration:
 *   2 revolutions of Motor 2 = 3.5 mm of linear travel
 *   → GUIDE_STEPS_PER_MM = (2 × 2048) / 3.5 ≈ 1170
 */

#include <Arduino.h>

// ── Pin assignments ──────────────────────────────────────────────────────────

const int M1[4]  = {0, 1, 2, 3};             // Winding motor  (ULN2003 #1)
const int M2[4]  = {4, 5, 6, 7};             // Traverse motor (ULN2003 #2)
const int SEG[8] = {8, 9, 10, 11, 12, 13, 14, 15};  // Segments A–G, DP
const int DIG[4] = {16, 17, 18, 19};         // Digit cathodes 0–3 (left→right)

#define ENC_CLK 20
#define ENC_DT  21
#define ENC_SW  22

// ── Motor constants ──────────────────────────────────────────────────────────

const int   STEPS_PER_REV      = 2048;       // 28BYJ-48 half-step (64:1 × 32 steps)
const float GUIDE_STEPS_PER_MM = (2.0f * STEPS_PER_REV) / 3.5f;  // ≈ 1170.3
const int   STEP_DELAY_MS      = 2;          // ms between winding motor steps

// ── AWG wire diameter presets ────────────────────────────────────────────────

struct WireGauge { uint8_t awg; float diameter_mm; };

const WireGauge GAUGES[] = {
  {22, 0.644f},
  {24, 0.511f},
  {26, 0.405f},
  {28, 0.321f},
  {30, 0.255f},
  {32, 0.202f},
  {34, 0.160f},
  {36, 0.127f},
  {38, 0.101f},
  {40, 0.079f},
};
const int NUM_GAUGES = (int)(sizeof(GAUGES) / sizeof(GAUGES[0]));

// ── Half-step sequence (IN1 = LSB) ───────────────────────────────────────────

const uint8_t HALF_STEP[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};

// ── 7-segment patterns (common cathode; bit0=A … bit6=G) ────────────────────

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

// Special characters
const uint8_t SEG_BLANK = 0x00;
const uint8_t SEG_DASH  = 0x40;  // g segment only  (–)
const uint8_t SEG_L     = 0x38;  // f, e, d          (L)
const uint8_t SEG_A_UP  = 0x77;  // a, b, c, e, f, g (A)
const uint8_t SEG_G_LO  = 0x6F;  // a, b, c, d, f, g (g) — same pattern as 9
const uint8_t SEG_O_LO  = 0x5C;  // c, d, e, g       (o)
const uint8_t SEG_D_LO  = 0x5E;  // b, c, d, e, g    (d)
const uint8_t SEG_N_LO  = 0x54;  // c, e, g          (n)
const uint8_t SEG_E_LO  = 0x79;  // a, d, e, f, g    (e)

// ── Display buffer ───────────────────────────────────────────────────────────

uint8_t displayBuf[4] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};

// ── State machine ────────────────────────────────────────────────────────────

enum State { MENU_LAYERS, MENU_LENGTH, MENU_GAUGE, MENU_START, WINDING };
State state = MENU_LAYERS;

// ── User-configurable settings ───────────────────────────────────────────────

int targetLayers  = 1;    // number of winding layers
int spoolLengthMM = 20;   // magnet/coil length in mm
int gaugeIndex    = 2;    // default: 28 AWG

// ── Winding state ────────────────────────────────────────────────────────────

int           computedTurns = 0;  // calculated at winding start

long          totalStepsM1 = 0;
long          totalStepsM2 = 0;
int           currentTurns = 0;
int           stepIdx1     = 0;
int           stepIdx2     = 0;
unsigned long lastStepTime = 0;

// ── Encoder ──────────────────────────────────────────────────────────────────

volatile int encoderDelta = 0;

void onEncoderCLK() {
  static uint32_t lastISR = 0;
  uint32_t now = millis();
  if (now - lastISR < 1) return;  // 1 ms debounce
  lastISR = now;
  // CLK is now LOW (falling edge); DT state indicates direction
  encoderDelta += (digitalRead(ENC_DT) == HIGH) ? 1 : -1;
}

// ── Motor helpers ────────────────────────────────────────────────────────────

void writeMotor(const int pins[4], uint8_t nibble) {
  for (int i = 0; i < 4; i++)
    digitalWrite(pins[i], (nibble >> i) & 1);
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

void releaseMotors() {
  writeMotor(M1, 0);
  writeMotor(M2, 0);
}

// ── Display helpers ──────────────────────────────────────────────────────────

// Call frequently from loop(); advances one digit per call (multiplexing).
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

// ── Startup motor test ───────────────────────────────────────────────────────
// Steps each motor 4 forward then 4 back so the user can confirm wiring
// before the first winding run. Display shows "----" during the test.

void motorStartupTest() {
  for (int i = 0; i < 4; i++) displayBuf[i] = SEG_DASH;
  for (int i = 0; i < 4; i++) { stepMotor1( 1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor1(-1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor2( 1); refreshDisplay(); delay(40); }
  for (int i = 0; i < 4; i++) { stepMotor2(-1); refreshDisplay(); delay(40); }
  releaseMotors();
}

// ── Menu display update ──────────────────────────────────────────────────────

void updateDisplay() {
  switch (state) {

    case MENU_LAYERS:
      showNumber(targetLayers);
      break;

    case MENU_LENGTH: {
      int n = constrain(spoolLengthMM, 0, 999);
      displayBuf[0] = SEG_L;
      displayBuf[1] = (n >= 100) ? DIGIT_PAT[n / 100]        : SEG_BLANK;
      displayBuf[2] = (n >= 10)  ? DIGIT_PAT[(n / 10) % 10]  : SEG_BLANK;
      displayBuf[3] = DIGIT_PAT[n % 10];
      break;
    }

    case MENU_GAUGE:
      displayBuf[0] = SEG_A_UP;
      displayBuf[1] = SEG_BLANK;
      displayBuf[2] = DIGIT_PAT[GAUGES[gaugeIndex].awg / 10];
      displayBuf[3] = DIGIT_PAT[GAUGES[gaugeIndex].awg % 10];
      break;

    case MENU_START:
      displayBuf[0] = SEG_G_LO;
      displayBuf[1] = SEG_O_LO;
      displayBuf[2] = SEG_BLANK;
      displayBuf[3] = SEG_BLANK;
      break;

    case WINDING: {
      showNumber(currentTurns);
      break;
    }
  }
}

// ── Encoder handler (called from loop when not winding) ──────────────────────

void handleEncoder() {
  noInterrupts();
  int delta = encoderDelta;
  encoderDelta = 0;
  interrupts();

  if (delta == 0) return;

  switch (state) {
    case MENU_LAYERS:
      targetLayers = constrain(targetLayers + delta, 1, 99);
      break;
    case MENU_LENGTH:
      spoolLengthMM = constrain(spoolLengthMM + delta, 1, 999);
      break;
    case MENU_GAUGE:
      gaugeIndex = (gaugeIndex + delta + NUM_GAUGES) % NUM_GAUGES;
      break;
    default:
      break;
  }
}

// ── Button handler ───────────────────────────────────────────────────────────

void handleButton() {
  if (digitalRead(ENC_SW) != LOW) return;

  delay(20);
  while (digitalRead(ENC_SW) == LOW);  // wait for release
  delay(20);

  switch (state) {
    case MENU_LAYERS:  state = MENU_LENGTH; break;
    case MENU_LENGTH:  state = MENU_GAUGE;  break;
    case MENU_GAUGE:   state = MENU_START;  break;

    case MENU_START: {
      // Compute total turns from layers × turns-per-layer
      float wireDiam = GAUGES[gaugeIndex].diameter_mm;
      int turnsPerLayer = max(1, (int)((float)spoolLengthMM / wireDiam));
      computedTurns = targetLayers * turnsPerLayer;
      state        = WINDING;
      currentTurns = 0;
      totalStepsM1 = 0;
      totalStepsM2 = 0;
      lastStepTime = millis();
      break;
    }

    case WINDING:
      state = MENU_LAYERS;
      releaseMotors();
      break;
  }
}

// ── Winding loop ─────────────────────────────────────────────────────────────

void handleWinding() {
  if (currentTurns >= computedTurns) {
    releaseMotors();
    displayBuf[0] = SEG_D_LO;
    displayBuf[1] = SEG_O_LO;
    displayBuf[2] = SEG_N_LO;
    displayBuf[3] = SEG_E_LO;
    unsigned long t = millis();
    while (millis() - t < 2000) refreshDisplay();  // hold "done" for 2 s
    state = MENU_LAYERS;
    return;
  }

  // Handle emergency stop check within the step delay
  if (digitalRead(ENC_SW) == LOW) {
    state = MENU_LAYERS;
    releaseMotors();
    delay(200); // Debounce
    return;
  }

  if (millis() - lastStepTime < (unsigned long)STEP_DELAY_MS) return;
  lastStepTime = millis();

  stepMotor1(1);

  // Synchronized traverse: calculates target guide position based on current rotation
  // Current rotation in mm of linear wire lay (totalRevs * wireDiameter)
  float wireDiam = GAUGES[gaugeIndex].diameter_mm;
  float totalLinearMM = ((float)totalStepsM1 / STEPS_PER_REV) * wireDiam;
  
  // Which pass (layer) are we on?
  int passNum = (int)(totalLinearMM / spoolLengthMM);
  
  // Distance into the current pass
  float posInPass = fmod(totalLinearMM, (float)spoolLengthMM);
  
  // Actual guide target position (ping-pong)
  float guideMM = (passNum % 2 == 0) ? posInPass : (float)spoolLengthMM - posInPass;
  
  long targetStepsM2 = (long)(guideMM * GUIDE_STEPS_PER_MM);

  // Move guide motor toward target
  if (totalStepsM2 < targetStepsM2) {
    stepMotor2(1);
  } else if (totalStepsM2 > targetStepsM2) {
    stepMotor2(-1);
  }

  currentTurns = (int)(totalStepsM1 / STEPS_PER_REV);
}

// ── Arduino entry points ─────────────────────────────────────────────────────

void setup() {
  for (int i = 0; i < 4; i++) { pinMode(M1[i],  OUTPUT); digitalWrite(M1[i],  LOW);  }
  for (int i = 0; i < 4; i++) { pinMode(M2[i],  OUTPUT); digitalWrite(M2[i],  LOW);  }
  for (int i = 0; i < 8; i++) { pinMode(SEG[i], OUTPUT); digitalWrite(SEG[i], LOW);  }
  for (int i = 0; i < 4; i++) { pinMode(DIG[i], OUTPUT); digitalWrite(DIG[i], HIGH); }

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, FALLING);

  motorStartupTest();
  updateDisplay();
}

void loop() {
  refreshDisplay();
  handleButton();

  if (state == WINDING) {
    handleWinding();
  } else {
    handleEncoder();
  }

  updateDisplay();
}
