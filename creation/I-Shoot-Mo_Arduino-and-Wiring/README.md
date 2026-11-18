# I-Shoot Mo - Arduino sketch

Open `ishootmo_code/ishootmo_code.ino` in Arduino IDE. Keep the file inside
the folder named `ishootmo_code`.

## Install and upload

1. Add this URL in File > Preferences > Additional Boards Manager URLs:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Install **esp32 by Espressif Systems** in Boards Manager.
3. Select **WEMOS LOLIN32** and the board's COM port.
4. Install **Adafruit GFX Library**, **Adafruit SH110X** and
   **Adafruit SSD1306** in Library Manager. Accept dependencies, including
   Adafruit BusIO.
5. Set `OLED_IS_SH1106` near the top of the sketch:
   - `1`: SH1106 128 x 64 I2C (default).
   - `0`: SSD1306 128 x 64 I2C.
   Verify the actual driver; screen size alone does not identify it.
6. Connect a Micro-USB data cable, click Verify, then Upload.
7. Open Serial Monitor at **115200 baud**.

OLED address defaults to `0x3C`. Change it only if a scan finds your module
at another address. If uploading remains on Connecting, hold BOOT while
connecting and release when writing starts.

## Two modes

**Precision:** Six throws. Each hole is selected twice in shuffled order.
Only the lit hole earns its 10/20/30 points. Wrong holes and manual MISS
presses use a throw and advance the target. Maximum score: **120**.
Consecutive selections of the same hole are allowed in this mode.

**Target Chase:** 60 seconds, unlimited throws. Targets are offered in
shuffled groups containing each hole once. Only correct hits advance;
wrong holes and MISS keep the target lit. Successive targets differ.
Every round starts the same prepared sequence, including after power-off.
Keep `CHASE_SEQUENCE_SEED` unchanged for all players in a competition.

## Buttons

| Button | Action |
| --- | --- |
| START/MODE | Tap while idle to change modes; hold 0.9 second to start. Press on the final screen to return to idle. |
| RESET | Cancel and clear the round; retain the selected mode. |
| MISS | Press once only when the beanbag misses every hole. |

Unlit-hole detections automatically increase **Wrong**. Do not press MISS
for a throw already recorded by any hole sensor. The throw lockout is
650 ms for both sensor and manual MISS events. Leave at least that long
between throws. Multiple simultaneous hole detections are ignored and
reported in Serial Monitor; investigate the sensor setup if this occurs.

## Wiring

| Connection | GPIO |
| --- | --- |
| 10 / 20 / 30-point sensor OUT | 34 / 35 / 32 |
| 10 / 20 / 30-point LED, through separate 220-ohm resistors | 25 / 26 / 27 |
| START/MODE / RESET / MISS | 18 / 19 / 23 |
| Low-trigger active buzzer SIG | 33 |
| OLED SDA / SCL | 21 / 22 |

Buttons connect their GPIO to GND and use internal pull-ups. Sensor OUT
must be HIGH when clear and LOW on detection. Verify each sensor and
buzzer module operates at 3.3 V. Component VCC goes to 3V3; all grounds
are common. Power the ESP32 itself through Micro-USB. Never apply a 5 V
signal to an ESP32 GPIO.

The updated build guide contains a complete component-level assembly view
on **page 7** and detailed wiring illustrations on **pages 8-10**.
Wire IDs and cut lengths are on **pages 11-12**, upload instructions on
**page 24**, and the complete sketch on **pages 25-31**. Separate wiring
images are in `creation/wiring` in the project, or `wiring` in the ZIP.

## Verification

Compiled for `esp32:esp32:lolin32` with installed ESP32 core **3.3.11**.
Both display settings compile. Eight compile-time rule checks cover:

- Precision balance over 100 seeds, maximum 120, and the six-throw limit.
- Precision wrong holes and misses consuming throws.
- Chase balance, repeatability, and different successive targets.
- Chase failures retaining the target.
- Immediate first throw and duplicate-event lockout.
- The 60-second deadline and rejection of late scores.
- Timer rollover.
- Reset and invalid inputs.

These checks do not test physical wiring, sensor calibration, display
identity, button contacts or power stability. No firmware was uploaded
to the assembled board during this revision. Follow the guide's final
hardware acceptance checklist after uploading.

This version runs locally and prints game events over USB Serial.
Wi-Fi reporting and persistent leaderboards are not included.
