#include <Arduino.h>

// Test ONE powered 3.3V-compatible digital IR module at a time.
// 10-point OUT=34, 20-point OUT=35, 30-point OUT=32.
// VCC -> 3V3, GND -> GND. Confirm the actual module pin labels.
constexpr uint8_t TEST_PIN = 34;
int lastState;
uint32_t lastReport;
unsigned long fallingEdges = 0;
unsigned long risingEdges = 0;

void setup() {
  Serial.begin(115200);
  // GPIO34/35 have no internal pull-ups. Match the game's plain INPUT.
  pinMode(TEST_PIN, INPUT);
  delay(1200);
  lastState = digitalRead(TEST_PIN);
  lastReport = millis();
  Serial.printf("\nIR TEST GPIO %u | initial raw=%d\n", TEST_PIN, lastState);
  Serial.println("Game expects clear=1, detected=0. Move a bag through and then away.");
  Serial.println("Raw edges are NOT automatically scored as throws or marked PASS.");
}

void loop() {
  const uint32_t now = millis();
  const int state = digitalRead(TEST_PIN);
  if (state != lastState) {
    lastState = state;
    if (state == LOW) ++fallingEdges;
    else ++risingEdges;
    Serial.printf("%lu ms | raw=%d | falling=%lu rising=%lu\n",
                  (unsigned long)now, state, fallingEdges, risingEdges);
  }
  if (uint32_t(now - lastReport) >= 1000) {
    lastReport = now;
    Serial.printf("GPIO %u raw=%d | falling=%lu rising=%lu\n",
                  TEST_PIN, state, fallingEdges, risingEdges);
  }
  delay(1);
}
