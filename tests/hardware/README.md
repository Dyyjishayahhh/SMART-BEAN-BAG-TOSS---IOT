# Test every component before fixing or assembling I-Shoot Mo

These are separate diagnostic sketches. The game sketches have not been changed.
Uploading a test replaces the program running on the ESP32; your saved game files
remain available for restoring it later. No test has been uploaded by Codex.

## Start here: ESP32 alone

1. Photograph/label existing connections if already assembled. Unplug USB and
   any battery or other supply before moving wires.
2. Disconnect all external components. Connect only the ESP32 to the computer
   with a USB **data** cable.
3. Open `01_board/01_board.ino` in Arduino IDE. Select **WEMOS LOLIN32**
   (or **ESP32 Dev Module** if necessary) and the board's COM port.
4. Upload, then open Serial Monitor at **115200 baud**. Press the board's reset
   button once if you missed the startup message.
5. PASS: one `ALIVE` line each second, with uptime increasing, for 60 seconds.
   Uptime restarts after you deliberately press reset; repeated restarts without
   pressing reset need investigation before connecting other components.

No COM port: try a known data cable and another USB port, and check Windows Device
Manager. Upload stuck on Connecting: hold BOOT while connecting and release when
writing starts, if the board has a BOOT button. An upload/cable failure alone does
not prove the ESP32 is defective.

## Power, wires, and breadboard

- Use USB to power the ESP32 during these tests; leave batteries and separate
  supplies disconnected. Use the board's **3V3/3V** pin for compatible modules.
- With a multimeter in DC voltage mode, check 3V3 to GND: approximately 3.3 V.
  After connecting each module, check that voltage remains approximately 3.3 V.
  A rail collapse or board restart after adding a part needs investigation.
- With **all power disconnected**, use continuity mode to check each jumper
  end to end and each rail section you use. Some breadboard rails are split in
  the middle. The 3V3 and GND rails must not remain shorted together.
  Capacitors can produce a brief transient indication; do not use that alone
  to declare a short. Never use continuity/resistance mode on a powered board.
- Check each LED resistor with power off and at least one lead disconnected
  from the circuit. In resistance mode, expect its marked 220-ohm value within
  its tolerance (for a gold 5% band: 209-231 ohms). Record all three readings.
  Without a meter, confirm the value markings and leave resistance unmeasured.
- Without a meter, use short known-good jumpers directly to the ESP32 first.
  Record voltage/continuity checks as **not tested**, not passed.
- Read the printed VCC/GND/OUT/SIG labels. Module pin order varies. Confirm the
  actual sensor and buzzer support 3.3 V; an unknown module's voltage rating
  cannot be inferred from this project's diagram. Do not feed 5 V into a GPIO.

## One component at a time

Unplug power before each wiring change. Connect only the component under test,
reconnect USB, upload its sketch, and open Serial Monitor at 115200. For repeated
parts, change `TEST_PIN` near the top and upload again. GPIO numbers mean the
board's printed GPIO labels, not physical header positions.

| Order | Component | Wiring | Sketch / setting | PASS evidence |
| --- | --- | --- | --- | --- |
| 1 | START/MODE button | GPIO18 -> button -> GND | `02_button`, `TEST_PIN=18` | Released=1, pressed=0; ten presses give ten counts |
| 2 | RESET button | GPIO19 -> button -> GND | `02_button`, `TEST_PIN=19` | Same; hold for 3 seconds adds only one count |
| 3 | MISS button | GPIO23 -> button -> GND | `02_button`, `TEST_PIN=23` | Same; returns to released after each press |
| 4 | 10-point LED | GPIO25 -> 220-ohm resistor -> anode; cathode -> GND | `03_led`, `TEST_PIN=25` | 1 second on, 1 second off for ten cycles |
| 5 | 20-point LED | Same using GPIO26 and its own resistor | `03_led`, `TEST_PIN=26` | Same |
| 6 | 30-point LED | Same using GPIO27 and its own resistor | `03_led`, `TEST_PIN=27` | Same |
| 7 | 10-point IR sensor | VCC=3V3, GND=GND, OUT=GPIO34 | `04_ir_sensor`, `TEST_PIN=34` | Clear=1, detected=0, then clear=1; ten controlled trials |
| 8 | 20-point IR sensor | Same, OUT=GPIO35 | `04_ir_sensor`, `TEST_PIN=35` | Same |
| 9 | 30-point IR sensor | Same, OUT=GPIO32 | `04_ir_sensor`, `TEST_PIN=32` | Same |
| 10 | Three-pin active buzzer | VCC=3V3, GND=GND, SIG=GPIO33 | `05_active_buzzer`; send `b` | Silent at idle, three short beeps on command, then stays silent |
| 11 | SH1106 OLED | VCC=3V3, GND=GND, SDA=GPIO21, SCL=GPIO22 | `06_oled`; send `s`, then `d` | Scan address + readable changing counter, complete border, alternating light/dark |

All module grounds connect to ESP32 GND. Never connect a bare LED without its
series resistor. A typical new LED's long leg is the anode (+); the flat side of
the package usually marks the cathode (-). Use its markings/data if legs are cut.

### Buttons

The sketch enables an internal pull-up, so no external button pull-up is needed.
For four-leg tactile switches, two legs are often already joined internally;
use contacts that change from open to connected when pressed. If a button reads
pressed constantly, inspect which contacts you used before replacing it.

### IR sensors

Start at short range with the actual beanbag. Test stationary clear/detected
states, then move the bag through at a realistic speed. Repeat under the expected
room lighting. There should be no unexplained switching while the scene is still.
If present, gently adjust the module's sensitivity trimmer and record the result.
Do not force the trimmer past its stops. A module indicator light alone does not
prove OUT reaches the ESP32.

GPIO34/35 have **no internal pull-up**. Disconnected inputs can float; random
readings with no sensor connected are not a sensor test. A confirmed open-collector
output without a module pull-up may need an external pull-up (for example 10k ohm
to 3V3); identify the actual module/output circuit before adding one. Measure OUT
relative to GND if a meter is available: the clear state should be near 3.3 V and
the detection state near 0 V for the current game.

Raw edge totals can include chatter. They are observations, not throw counts.
Stable HIGH-on-detect behavior is useful evidence of opposite polarity, not proof
the sensor is broken. A reflective sensor that sees a hand but misses the bag at
the required mounting distance has not passed the intended-use test.

### Buzzer

The latest main sketch says active LOW; older notes use active HIGH. The test
starts with `ACTIVE_LOW=true`. It must be silent before you send `b`. If it sounds
at idle, unplug USB, change the test's `ACTIVE_LOW` to `false`, upload, and retry.
Record the setting that gives three beeps followed by silence. If neither works,
check labels, ground, voltage rating, and module type. This sketch is for the
three-pin active module; a bare/passive buzzer needs a different test/driver.
Brief sound during board boot is a separate observation to record.

### OLED

Only this sketch needs **Adafruit GFX Library** and **Adafruit SH110X**, plus their
dependencies (including Adafruit BusIO). Install with Arduino Library Manager.
Other tests use only the ESP32 board package.

Type `s` in Serial Monitor and send it. With only the OLED connected, expect its
address, commonly `0x3C` or `0x3D`. If it is `0x3D`, change `OLED_ADDRESS` in the
test and upload again. Send `d` after the address matches. The screen should show
`OLED TEST`, a counter that increases every second, and a border along all edges.
It alternates normal/inverted colors to help inspect pixels. An I2C response
does not establish the display driver or prove the pixels work. This test uses
SH1106 because that is the current project driver; an unknown/SSD1306 display
needs its matching driver before declaring it faulty.

## Record results before making repairs

Do not label a part broken after only one failed setup. Record exactly what
happened, then isolate it using a known-good wire, port, or equivalent component.
For example: test two sensors sequentially on the same GPIO using the same wires;
then test the working sensor on the suspect GPIO. This separates sensor faults
from wiring or pin faults. Power off between every swap.

| Item | Status (not tested / pass / needs investigation) | Observation |
| --- | --- | --- |
| ESP32 + USB data cable | Not tested | |
| 3.3 V supply | Not tested | |
| Jumpers + breadboard rails | Not tested | |
| Three 220-ohm resistors | Not tested | R10 = ?; R20 = ?; R30 = ? |
| START/MODE button | Not tested | |
| RESET button | Not tested | |
| MISS button | Not tested | |
| 10-point LED + resistor | Not tested | |
| 20-point LED + resistor | Not tested | |
| 30-point LED + resistor | Not tested | |
| 10-point IR sensor | Not tested | |
| 20-point IR sensor | Not tested | |
| 30-point IR sensor | Not tested | |
| Active buzzer | Not tested | ACTIVE_LOW = ? |
| OLED | Not tested | Address = ?; driver = ? |

Once individual tests pass, reconnect one component at a time with power off
between changes. Then restore your chosen game sketch and check the assembled
system: each button action, each LED/target, each hole, wrong-hole and MISS
behavior, timer, display, and sound. Passing separately does not establish that
the full wiring, sensor placement, or shared supply works together.

## Technical references

- [Espressif GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html): GPIO34-39 are input-only and have no software pull-ups/pull-downs.
- [ESP32 datasheet](https://documentation.espressif.com/esp32_datasheet_en.html): electrical characteristics and pin limits.
- Component pin assignments come from this project's `creation/wiring/source/connection-map.json` and current main sketch. Actual module identity and ratings still need inspection.
