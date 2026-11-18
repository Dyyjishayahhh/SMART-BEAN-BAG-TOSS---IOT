#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1106G display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Use 0x3D here if that was your scanner result.
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED initialization failed");
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("OLED OK");

  display.setTextSize(1);
  display.println();
  display.println("SH1106 test");
  display.display();
}

void loop() {
}