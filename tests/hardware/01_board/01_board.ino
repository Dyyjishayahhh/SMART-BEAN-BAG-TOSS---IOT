#include <Arduino.h>

// ESP32 only: disconnect external components for this first test.
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\nBOARD TEST: ESP32 started. Expect one ALIVE line per second.");
}

void loop() {
  Serial.printf("ALIVE | uptime %lu seconds\n", (unsigned long)(millis() / 1000));
  delay(1000);
}
