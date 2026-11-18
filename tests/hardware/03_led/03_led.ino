#include <Arduino.h>

// Test ONE LED at a time: 10-point=25, 20-point=26, 30-point=27.
// GPIO -> 220-ohm resistor -> LED anode; LED cathode -> GND.
constexpr uint8_t TEST_PIN = 25;

void setup() {
  Serial.begin(115200);
  pinMode(TEST_PIN, OUTPUT);
  digitalWrite(TEST_PIN, LOW);
  delay(1200);
  Serial.printf("\nLED TEST GPIO %u: one second ON, one second OFF.\n", TEST_PIN);
}

void loop() {
  digitalWrite(TEST_PIN, HIGH);
  Serial.println("LED ON");
  delay(1000);
  digitalWrite(TEST_PIN, LOW);
  Serial.println("LED OFF");
  delay(1000);
}
