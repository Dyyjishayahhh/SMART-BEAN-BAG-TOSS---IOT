#include <Arduino.h>

// For the project's THREE-PIN active buzzer module, not a bare buzzer.
// VCC -> 3V3, GND -> GND, SIG/S/IN -> GPIO33.
constexpr uint8_t BUZZER_PIN = 33;
// Latest main sketch uses true; verify the physical module with this test.
constexpr bool ACTIVE_LOW = true;
constexpr uint8_t ON_LEVEL = ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t OFF_LEVEL = ACTIVE_LOW ? HIGH : LOW;

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, OFF_LEVEL);
  delay(1200);
  Serial.printf("\nACTIVE BUZZER TEST | ACTIVE_LOW=%s\n", ACTIVE_LOW ? "true" : "false");
  Serial.println("Expected: silent now. Send b to hear three short beeps, then silence.");
  Serial.println("If sounding while idle: unplug USB, change ACTIVE_LOW, and upload again.");
}

void loop() {
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'b' || command == 'B') {
      for (int i = 0; i < 3; ++i) {
        Serial.printf("Beep %d ON\n", i + 1);
        digitalWrite(BUZZER_PIN, ON_LEVEL);
        delay(150);
        digitalWrite(BUZZER_PIN, OFF_LEVEL);
        Serial.println("OFF");
        delay(500);
      }
      Serial.println("Done. Must now stay silent. Send b to repeat.");
    }
  }
  delay(1);
}
