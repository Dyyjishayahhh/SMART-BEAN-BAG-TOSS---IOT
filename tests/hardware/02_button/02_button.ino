#include <Arduino.h>

// Test ONE button at a time. START=18, RESET=19, MISS=23.
constexpr uint8_t TEST_PIN = 18;
constexpr uint32_t DEBOUNCE_MS = 45;
int lastRaw;
int stable;
uint32_t changedAt;
uint32_t lastReport;
unsigned long presses = 0;

void setup() {
  Serial.begin(115200);
  pinMode(TEST_PIN, INPUT_PULLUP);
  delay(1200);
  lastRaw = stable = digitalRead(TEST_PIN);
  changedAt = lastReport = millis();
  Serial.printf("\nBUTTON TEST GPIO %u: connect button between this pin and GND.\n", TEST_PIN);
  Serial.println("Released = 1; pressed = 0. Press and release ten times.");
}

void loop() {
  const uint32_t now = millis();
  const int raw = digitalRead(TEST_PIN);
  if (raw != lastRaw) {
    lastRaw = raw;
    changedAt = now;
  }
  if (raw != stable && uint32_t(now - changedAt) >= DEBOUNCE_MS) {
    stable = raw;
    if (stable == LOW) ++presses;
    Serial.printf("%s | presses=%lu\n", stable == LOW ? "PRESSED" : "RELEASED", presses);
  }
  if (uint32_t(now - lastReport) >= 1000) {
    lastReport = now;
    Serial.printf("GPIO %u stable=%d | presses=%lu\n", TEST_PIN, stable, presses);
  }
  delay(1);
}
