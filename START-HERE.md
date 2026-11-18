# I-Shoot Mo - moving to another PC

Extract the entire ZIP before opening the project. Keep the folder structure.

## Main files

- **Arduino sketch:** `creation/code/ishootmo_code/ishootmo_code.ino`
- **Latest complete guide:** `creation/I-Shoot-Mo_ESP32_Complete-Build-Guide.pdf`
- **Complete wiring picture:** `creation/wiring/00-full-assembly.png`
- **Detailed wiring pictures:** the other three PNGs in `creation/wiring`
- **Arduino instructions:** `creation/code/README.md`
- **Proposals and editable documents:** `PROPOSAL-tu` and `creation`
- **Previous versions:** `creation/backups`
- **Game-rule checks:** `tests/firmware/contract_checks`
- **Guide and diagram editing tools:** `creation/tools`

Use the latest complete guide above for the agreed two-mode game. Older
construction manuals and backups are included as project references.

## Set up the new PC for Arduino

The ZIP contains the project files. Install Arduino IDE and the board/libraries
on the new PC separately:

1. Install Arduino IDE 2.x.
2. In File > Preferences, add this Additional Boards Manager URL:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In Boards Manager, install **esp32 by Espressif Systems**.
4. In Library Manager, install **Adafruit GFX Library**, **Adafruit SH110X**
   and **Adafruit SSD1306**. Accept the required dependencies, including
   Adafruit BusIO.
5. Open `creation/code/ishootmo_code/ishootmo_code.ino`. Keep the file inside
   its folder named `ishootmo_code`.
6. Select **WEMOS LOLIN32** and the new PC's correct COM port. If that board
   is not listed, use **ESP32 Dev Module**.
7. Check `OLED_IS_SH1106` near the top of the sketch: `1` for SH1106 or `0`
   for SSD1306, both 128 x 64 I2C. Match the actual display driver.
8. Connect the ESP32 with a Micro-USB data cable. Click Verify, then Upload.
9. Open Serial Monitor at **115200 baud**.

The COM port may differ from the old PC. If no port appears, follow the
board manufacturer's instructions for its USB-to-serial driver.

## Controls

- Tap START/MODE while idle to select Precision or Target Chase.
- Hold START/MODE for 0.9 second to begin.
- Press RESET to cancel and clear the round.
- Press MISS once only when the beanbag misses every hole. Wrong-hole
  detections are already recorded automatically.

See the latest guide for the full rules, wiring, calibration and acceptance
checks. The project has been compiled and its game rules checked, but the
assembled hardware still needs testing after upload.

## Optional: edit or regenerate the guide

The Arduino sketch does not need Python. The documentation tools use Python
with `reportlab` and `pypdf` and expect the Windows Consolas font.
Run `creation/tools/revise_build_guide.py` from the extracted project.
Its original source guide is included in `creation/backups`.

This transfer package contains project files, reference documents and backups.
Installed applications, board packages, libraries, temporary build files and
Git's internal history are not bundled.
