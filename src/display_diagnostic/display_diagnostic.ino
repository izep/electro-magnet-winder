/*
 * Display Diagnostic Program V2
 * Corrected mapping for SH5461AS Standard Pinout 
 * (Matches README physical wiring but fixes logical assignments)
 */

// Logical mapping based on standard datasheet functions of the pins you connected:
const int segmentPins[] = {8, 27, 10, 11, 12, 15, 14}; // A, B, C, D, E, F, G
const char segmentNames[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};

// Digits based on standard datasheet functions of the pins you connected:
const int digitPins[] = {9, 13, 26, 28}; // Digit 1 (L), 2, 3, 4 (R)

void setup() {
  Serial.begin(115200);
  
  // Initialize all pins as OUTPUT
  for (int i = 0; i < 7; i++) {
    pinMode(segmentPins[i], OUTPUT);
    digitalWrite(segmentPins[i], LOW);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], HIGH); // Common Cathode: HIGH = OFF
  }
  
  Serial.println("Display Diagnostic V2 Started");
  Serial.println("Using corrected logical mapping for standard SH5461AS.");
}

void loop() {
  // Test 1: All segments of all digits (Verify Polarity CC vs CA)
  Serial.println("Testing Polarity: All segments ON (Common Cathode mode)");
  for (int d = 0; d < 4; d++) digitalWrite(digitPins[d], LOW);
  for (int s = 0; s < 7; s++) digitalWrite(segmentPins[s], HIGH);
  delay(2000);
  for (int s = 0; s < 7; s++) digitalWrite(segmentPins[s], LOW);
  for (int d = 0; d < 4; d++) digitalWrite(digitPins[d], HIGH);
  delay(500);

  // Test 2: Sequential Scan
  for (int d = 0; d < 4; d++) {
    Serial.print("Testing Digit "); Serial.println(d + 1);
    digitalWrite(digitPins[d], LOW); 
    
    for (int s = 0; s < 7; s++) {
      Serial.print("  Segment "); Serial.println(segmentNames[s]);
      digitalWrite(segmentPins[s], HIGH);
      delay(400);
      digitalWrite(segmentPins[s], LOW);
      delay(100);
    }
    digitalWrite(digitPins[d], HIGH);
    delay(300);
  }
}
