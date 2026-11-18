#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// Project display: SH1106 128x64 I2C. SDA=21, SCL=22.
// VCC -> 3V3, GND -> GND. Follow the module's printed labels.
constexpr uint8_t OLED_ADDRESS = 0x3C; // Change only to the scanned OLED address.
Adafruit_SH1106G display(128, 64, &Wire, -1);
bool displayReady = false;
uint32_t lastFrame = 0;
unsigned long frame = 0;

void scanBus() {
  int found = 0;
  Serial.println("I2C scan starting (only the OLED should be connected)...");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      ++found;
      Serial.printf("Device answered at 0x%02X\n", address);
    } else if (error != 2) {
      Serial.printf("Bus error %u at 0x%02X. Check power and SDA/SCL, then retry s.\n", error, address);
      return;
    }
  }
  Serial.printf("Scan complete: %d device(s). An address alone does not identify the driver.\n", found);
  Serial.println("If OLED address matches OLED_ADDRESS, send d for the visual test.");
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  delay(1200);
  Serial.println("\nOLED TEST: send s to scan, then d to start the SH1106 visual test.");
  Serial.println("Confirm a changing counter, complete border, and alternating light/dark screen.");
}

void loop() {
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 's' || command == 'S') scanBus();
    if ((command == 'd' || command == 'D') && !displayReady) {
      Wire.beginTransmission(OLED_ADDRESS);
      if (Wire.endTransmission() != 0) {
        Serial.printf("No device answered at 0x%02X. Send s first.\n", OLED_ADDRESS);
      } else if (!display.begin(OLED_ADDRESS, true)) {
        Serial.println("Display initialization failed. Check wiring/address/library.");
      } else {
        displayReady = true;
        Serial.println("Inspect the screen yourself; initialization alone is not a PASS.");
      }
    }
  }
  if (displayReady && uint32_t(millis() - lastFrame) >= 1000) {
    lastFrame = millis();
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.drawRect(0, 0, 128, 64, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 8);
    display.println("OLED TEST");
    display.setTextSize(1);
    display.setCursor(8, 34);
    display.print("Frame: ");
    display.print(++frame);
    display.setCursor(8, 48);
    display.print("SH1106 128x64");
    display.display();
    display.invertDisplay((frame % 2) == 0);
    Serial.printf("Visual frame %lu sent; inspect OLED.\n", frame);
  }
  delay(1);
}
