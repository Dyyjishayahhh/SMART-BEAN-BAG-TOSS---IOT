#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp_random.h>

// =====================================================
// I-SHOOT MO
// ESP32 / WEMOS LOLIN32
// SH1106 128x64 I2C OLED
//
// MODE 1: PRECISION
// Six throws. Each target appears twice. Maximum: 120.
//
// MODE 2: TARGET CHASE
// 60 seconds. Balanced target groups.
// Same prepared target sequence for every player.
// =====================================================

// ---------------- DISPLAY ----------------

#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(128, 64, &Wire, -1);

// ---------------- CONNECTIONS ----------------

// Order: 10-point, 20-point, 30-point hole.
const byte SENSOR_PINS[3] = {34, 35, 32};
const byte LED_PINS[3] = {25, 26, 27};
const int ZONE_POINTS[3] = {10, 20, 30};

const byte START_MODE_PIN = 18;
const byte RESET_PIN = 19;
const byte MISS_PIN = 23;

const byte BUZZER_PIN = 33;

// Your confirmed buzzer:
// LOW = sound ON
// HIGH = sound OFF
constexpr bool BUZZER_ACTIVE_LOW = true;

constexpr byte BUZZER_ON_LEVEL =
  BUZZER_ACTIVE_LOW ? LOW : HIGH;

constexpr byte BUZZER_OFF_LEVEL =
  BUZZER_ACTIVE_LOW ? HIGH : LOW;

// ---------------- GAME SETTINGS ----------------

constexpr int FIXED_THROWS = 6;

constexpr uint32_t TIMED_ROUND_MS = 60000;
constexpr uint32_t SENSOR_LOCKOUT_MS = 650;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 45;
constexpr uint32_t HOLD_TO_START_MS = 900;

constexpr uint32_t CHASE_SEQUENCE_SEED = 20261113;

// ---------------- SOUND SETTINGS ----------------

constexpr uint16_t STARTUP_BEEP_MS = 80;
constexpr uint16_t BUTTON_BEEP_MS = 50;
constexpr uint16_t MODE_BEEP_MS = 350;
constexpr uint16_t GAME_START_BEEP_MS = 80;
constexpr uint16_t SCORE_BEEP_MS = 120;
constexpr uint16_t COUNTDOWN_BEEP_MS = 80;
constexpr uint16_t FINISH_BEEP_MS = 350;

constexpr uint32_t SOUND_GAP_MS = 30;

// ---------------- GAME RULES ----------------

enum GameMode {
  PRECISION,
  TARGET_CHASE
};

enum ThrowResult {
  IGNORED,
  CORRECT_HIT,
  WRONG_HOLE,
  MISSED_THROW
};

struct GameState {
  GameMode mode = PRECISION;

  bool running = false;
  bool finished = false;

  int score = 0;
  int hits = 0;
  int wrong = 0;
  int misses = 0;
  int throwsUsed = 0;

  int zoneHits[3] = {0, 0, 0};
  int activeZone = -1;

  int order[6] = {0, 0, 0, 0, 0, 0};
  int orderLength = 0;
  int nextIndex = 0;

  uint32_t startedAt = 0;
  uint32_t lastThrowAt = 0;
  uint32_t randomState = 1;

  constexpr void clear() {
    running = false;
    finished = false;

    score = 0;
    hits = 0;
    wrong = 0;
    misses = 0;
    throwsUsed = 0;

    activeZone = -1;
    orderLength = 0;
    nextIndex = 0;
    startedAt = 0;
    lastThrowAt = 0;

    for (int i = 0; i < 3; ++i) {
      zoneHits[i] = 0;
    }
  }

  constexpr int randomBelow(int upper) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;

    return int(randomState % uint32_t(upper));
  }

  constexpr void shuffleTargets() {
    orderLength = mode == PRECISION ? 6 : 3;
    nextIndex = 0;

    for (int i = 0; i < orderLength; ++i) {
      order[i] = mode == PRECISION ? i / 2 : i;
    }

    for (int i = orderLength - 1; i > 0; --i) {
      int j = randomBelow(i + 1);

      int saved = order[i];
      order[i] = order[j];
      order[j] = saved;
    }

    // Chase always moves to a different hole.
    if (mode == TARGET_CHASE && order[0] == activeZone) {
      int saved = order[0];
      order[0] = order[1];
      order[1] = saved;
    }
  }

  constexpr void selectNextTarget() {
    if (nextIndex >= orderLength) {
      shuffleTargets();
    }

    activeZone = order[nextIndex++];
  }

  constexpr void start(
    GameMode selected,
    uint32_t now,
    uint32_t seed
  ) {
    clear();

    mode = selected;
    randomState = seed ? seed : 1;

    shuffleTargets();
    selectNextTarget();

    startedAt = now;

    // Allow the first throw immediately.
    lastThrowAt = now - SENSOR_LOCKOUT_MS;

    running = true;
  }

  constexpr void finish() {
    running = false;
    finished = true;
    activeZone = -1;
  }

  constexpr bool expire(uint32_t now) {
    if (running &&
        mode == TARGET_CHASE &&
        uint32_t(now - startedAt) >= TIMED_ROUND_MS) {
      finish();
      return true;
    }

    return false;
  }

  // -1 = manual MISS
  //  0 = 10-point sensor
  //  1 = 20-point sensor
  //  2 = 30-point sensor
  constexpr ThrowResult recordThrow(int zone, uint32_t now) {
    if (!running || zone < -1 || zone > 2 || expire(now)) {
      return IGNORED;
    }

    if (uint32_t(now - lastThrowAt) < SENSOR_LOCKOUT_MS) {
      return IGNORED;
    }

    lastThrowAt = now;
    ++throwsUsed;

    ThrowResult result = MISSED_THROW;

    if (zone == activeZone) {
      score += (zone + 1) * 10;

      ++hits;
      ++zoneHits[zone];

      result = CORRECT_HIT;
    } else if (zone >= 0) {
      ++wrong;
      result = WRONG_HOLE;
    } else {
      ++misses;
    }

    if (mode == PRECISION && throwsUsed >= FIXED_THROWS) {
      finish();
    } else if (mode == PRECISION || result == CORRECT_HIT) {
      // Precision advances after every recorded attempt.
      // Chase advances only after a correct hit.
      selectNextTarget();
    }

    return result;
  }
};

GameState game;

// ---------------- SOUND QUEUE ----------------

// Keep overlapping sounds separate.
struct SoundQueue {
  uint16_t durations[8] = {};

  byte head = 0;
  byte count = 0;

  constexpr bool push(uint16_t duration) {
    if (count == 8) {
      return false;
    }

    durations[(head + count) % 8] = duration;
    ++count;

    return true;
  }

  constexpr uint16_t pop() {
    if (!count) {
      return 0;
    }

    uint16_t result = durations[head];

    head = (head + 1) % 8;
    --count;

    return result;
  }

  constexpr void clear() {
    head = 0;
    count = 0;
  }
};

SoundQueue sounds;

bool buzzerActive = false;
bool soundGap = false;

uint32_t buzzerStartedAt = 0;
uint32_t buzzerDuration = 0;
uint32_t soundEndedAt = 0;

void stopSounds() {
  sounds.clear();

  buzzerActive = false;
  soundGap = false;

  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
}

void beep(uint32_t duration) {
  if (duration && !sounds.push(uint16_t(duration))) {
    Serial.println("Sound queue full");
  }
}

void updateBuzzer() {
  uint32_t now = millis();

  if (buzzerActive &&
      uint32_t(now - buzzerStartedAt) >= buzzerDuration) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);

    buzzerActive = false;
    soundEndedAt = now;
    soundGap = true;
  }

  if (!buzzerActive &&
      sounds.count &&
      (!soundGap ||
       uint32_t(now - soundEndedAt) >= SOUND_GAP_MS)) {
    buzzerDuration = sounds.pop();
    buzzerStartedAt = now;

    digitalWrite(BUZZER_PIN, BUZZER_ON_LEVEL);

    buzzerActive = true;
    soundGap = false;
  }
}

// ---------------- COUNTDOWN SOUND ----------------

struct CountdownSound {
  int lastSecond = -1;

  constexpr int takeCue(uint32_t elapsed) {
    uint32_t remaining = elapsed >= TIMED_ROUND_MS
                           ? 0
                           : TIMED_ROUND_MS - elapsed;

    int seconds = int((remaining + 999) / 1000);

    if (seconds > 10 || seconds == lastSecond) {
      return -1;
    }

    lastSecond = seconds;
    return seconds;
  }
};

CountdownSound countdown;

// ---------------- BUTTONS ----------------

struct ButtonState {
  byte pin;

  bool stable = HIGH;
  bool lastReading = HIGH;

  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;

  bool holdHandled = false;
};

ButtonState startButton = {START_MODE_PIN};
ButtonState resetButton = {RESET_PIN};
ButtonState missButton = {MISS_PIN};

// Explicit declaration for Arduino's sketch preprocessor.
int readButton(ButtonState &button, bool allowHold);

// ---------------- SENSOR AND DISPLAY STATE ----------------

bool lastSensorState[3] = {HIGH, HIGH, HIGH};

bool displayDirty = true;
uint32_t lastDisplayAt = 0;

// ---------------- GAME FUNCTIONS ----------------

void updateCountdownSound() {
  if (!game.running || game.mode != TARGET_CHASE) {
    return;
  }

  int second = countdown.takeCue(
    uint32_t(millis() - game.startedAt)
  );

  if (second >= 0) {
    beep(COUNTDOWN_BEEP_MS);

    Serial.print("COUNTDOWN,");
    Serial.println(second);

    displayDirty = true;
  }
}

void updateTargetLeds() {
  for (int i = 0; i < 3; ++i) {
    digitalWrite(
      LED_PINS[i],
      game.running && game.activeZone == i ? HIGH : LOW
    );
  }
}

void printTarget() {
  if (!game.running || game.activeZone < 0) {
    return;
  }

  Serial.print("TARGET,points=");
  Serial.println(ZONE_POINTS[game.activeZone]);
}

void resetSession() {
  // Keep the selected mode.
  game.clear();

  updateTargetLeds();
  stopSounds();

  countdown.lastSecond = -1;
  displayDirty = true;

  Serial.println("RESET");
}

void beginGame() {
  uint32_t seed = game.mode == PRECISION
                    ? esp_random()
                    : CHASE_SEQUENCE_SEED;

  game.start(game.mode, millis(), seed);

  countdown.lastSecond = -1;

  updateTargetLeds();
  beep(GAME_START_BEEP_MS);

  displayDirty = true;

  Serial.print("START,mode=");
  Serial.println(
    game.mode == PRECISION ? "PRECISION" : "TARGET_CHASE"
  );

  printTarget();
}

void announceFinish() {
  updateTargetLeds();

  if (game.mode == TARGET_CHASE) {
    // Zero seconds gets exactly one countdown beep.
    if (countdown.lastSecond != 0) {
      countdown.lastSecond = 0;

      beep(COUNTDOWN_BEEP_MS);
      Serial.println("COUNTDOWN,0");
    }
  } else {
    beep(FINISH_BEEP_MS);
  }

  displayDirty = true;

  Serial.print("END,score=");
  Serial.print(game.score);

  Serial.print(",hits=");
  Serial.print(game.hits);

  Serial.print(",wrong=");
  Serial.print(game.wrong);

  Serial.print(",misses=");
  Serial.print(game.misses);

  Serial.print(",throws=");
  Serial.print(game.throwsUsed);

  for (int i = 0; i < 3; ++i) {
    Serial.print(",zone");
    Serial.print(ZONE_POINTS[i]);
    Serial.print('=');
    Serial.print(game.zoneHits[i]);
  }

  Serial.println();
}

void handleThrow(int zone) {
  bool wasRunning = game.running;

  ThrowResult result = game.recordThrow(zone, millis());

  if (result != IGNORED) {
    Serial.print(
      result == CORRECT_HIT ? "HIT" :
      result == WRONG_HOLE ? "WRONG" : "MISS"
    );

    Serial.print(",score=");
    Serial.print(game.score);

    Serial.print(",throws=");
    Serial.println(game.throwsUsed);

    displayDirty = true;

    // Every scoring hit gets a short beep.
    if (result == CORRECT_HIT) {
      beep(SCORE_BEEP_MS);
    }
  }

  if (wasRunning && game.finished) {
    announceFinish();
  } else if (result != IGNORED) {
    updateTargetLeds();
    printTarget();
  }
}

// ---------------- INPUT FUNCTIONS ----------------

// 0 = none, 1 = press, 2 = release, 3 = hold.
int readButton(ButtonState &button, bool allowHold) {
  bool reading = digitalRead(button.pin);
  uint32_t now = millis();

  if (reading != button.lastReading) {
    button.changedAt = now;
    button.lastReading = reading;
  }

  int event = 0;

  if (uint32_t(now - button.changedAt) >= BUTTON_DEBOUNCE_MS &&
      reading != button.stable) {
    button.stable = reading;

    if (reading == LOW) {
      button.pressedAt = now;
      button.holdHandled = false;
      event = 1;
    } else {
      event = 2;
    }
  }

  if (allowHold &&
      button.stable == LOW &&
      !button.holdHandled &&
      uint32_t(now - button.pressedAt) >= HOLD_TO_START_MS) {
    button.holdHandled = true;
    event = 3;
  }

  return event;
}

byte pollSensors() {
  byte edges = 0;

  for (int i = 0; i < 3; ++i) {
    bool reading = digitalRead(SENSOR_PINS[i]);

    if (reading == LOW && lastSensorState[i] == HIGH) {
      edges |= byte(1 << i);
    }

    lastSensorState[i] = reading;
  }

  return edges;
}

int topZone() {
  if (game.hits == 0) {
    return -1;
  }

  int best = 0;

  for (int i = 1; i < 3; ++i) {
    if (game.zoneHits[i] > game.zoneHits[best]) {
      best = i;
    }
  }

  return best;
}

// ---------------- OLED SCREEN ----------------

void drawScreen() {
  display.clearDisplay();

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (!game.running && !game.finished) {
    display.println("I-SHOOT MO");

    display.setCursor(0, 16);
    display.println(
      game.mode == PRECISION
        ? "1 PRECISION"
        : "2 TARGET CHASE"
    );

    display.setCursor(0, 28);
    display.println(
      game.mode == PRECISION
        ? "6 throws / max 120"
        : "60s / unlimited throws"
    );

    display.setCursor(0, 44);
    display.println("Tap: change mode");

    display.setCursor(0, 56);
    display.println("Hold: start game");
  } else if (game.running) {
    display.setTextSize(2);
    display.print("S:");
    display.println(game.score);

    display.setTextSize(1);
    display.setCursor(0, 20);

    display.print("Aim: ");
    display.print(ZONE_POINTS[game.activeZone]);
    display.println(" pts");

    display.setCursor(0, 32);

    display.print("Hit:");
    display.print(game.hits);

    display.print(" Wrong:");
    display.println(game.wrong);

    display.setCursor(0, 44);

    display.print("Miss:");
    display.println(game.misses);

    display.setCursor(0, 56);

    if (game.mode == PRECISION) {
      display.print("Throws left: ");
      display.println(FIXED_THROWS - game.throwsUsed);
    } else {
      uint32_t elapsed = uint32_t(millis() - game.startedAt);

      uint32_t remaining = elapsed >= TIMED_ROUND_MS
                             ? 0
                             : TIMED_ROUND_MS - elapsed;

      display.print("Time: ");
      display.print((remaining + 999) / 1000);
      display.println("s");
    }
  } else {
    display.println(
      game.mode == TARGET_CHASE
        ? "TIME UP - 0s"
        : "FINAL RESULT"
    );

    display.setTextSize(2);
    display.setCursor(0, 12);

    display.print("S:");
    display.println(game.score);

    display.setTextSize(1);
    display.setCursor(0, 32);

    display.print("Hit:");
    display.print(game.hits);

    display.print(" Wrong:");
    display.println(game.wrong);

    display.setCursor(0, 44);

    display.print("Miss:");
    display.println(game.misses);

    display.setCursor(0, 56);
    display.print("Top zone: ");

    int best = topZone();

    if (best < 0) {
      display.println("None");
    } else {
      display.println(ZONE_POINTS[best]);
    }
  }

  display.display();

  lastDisplayAt = millis();
  displayDirty = false;
}

// ---------------- SETUP ----------------

void setup() {
  Serial.begin(115200);

  // HIGH silences your LOW-trigger buzzer.
  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
  pinMode(BUZZER_PIN, OUTPUT);
  stopSounds();

  Wire.begin(21, 22);

  for (int i = 0; i < 3; ++i) {
    pinMode(SENSOR_PINS[i], INPUT);

    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);

    lastSensorState[i] = digitalRead(SENSOR_PINS[i]);
  }

  // Buttons connect between their GPIO and GND.
  pinMode(START_MODE_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(MISS_PIN, INPUT_PULLUP);

  Wire.beginTransmission(OLED_ADDRESS);
  bool oledPresent = Wire.endTransmission() == 0;

  bool oledReady =
    oledPresent && display.begin(OLED_ADDRESS, true);

  if (!oledReady) {
    Serial.println(
      "OLED error: check address, SDA21/SCL22, and power."
    );

    stopSounds();

    while (true) {
      delay(1000);
    }
  }

  Serial.println("I-SHOOT MO / SH1106 / LOW-TRIGGER BUZZER");

  Serial.println(
    "READY: tap START to select; hold 0.9s to play."
  );

  drawScreen();

  // Short startup beep, then silence.
  beep(STARTUP_BEEP_MS);
}

// ---------------- MAIN LOOP ----------------

void loop() {
  updateBuzzer();

  byte edges = pollSensors();

  int startEvent = readButton(startButton, !game.running);
  int resetEvent = readButton(resetButton, false);
  int missEvent = readButton(missButton, false);

  if (resetEvent == 1) {
    // RESET cancels the round and clears queued sounds.
    startButton.holdHandled = true;

    resetSession();
    beep(BUTTON_BEEP_MS);
  } else {
    // Short beep for each debounced button press.
    if (startEvent == 1) {
      beep(BUTTON_BEEP_MS);
    }

    if (missEvent == 1) {
      beep(BUTTON_BEEP_MS);
    }

    if (!game.running) {
      if (game.finished && startEvent == 1) {
        // Return from the result screen to the menu.
        startButton.holdHandled = true;

        resetSession();
        beep(BUTTON_BEEP_MS);
      } else if (!game.finished && startEvent == 3) {
        // Hold START/MODE to start.
        beginGame();
      } else if (!game.finished &&
                 startEvent == 2 &&
                 !startButton.holdHandled) {
        // Tap START/MODE to change modes.
        game.mode = game.mode == PRECISION
                      ? TARGET_CHASE
                      : PRECISION;

        // Long beep confirms the selected mode.
        beep(MODE_BEEP_MS);

        displayDirty = true;
      }
    } else {
      // One beep per remaining second: 10, 9, ... 0.
      updateCountdownSound();

      // Reject throws when the timer has expired.
      if (game.expire(millis())) {
        announceFinish();
      } else if (edges == 1 || edges == 2 || edges == 4) {
        handleThrow(
          edges == 1 ? 0 :
          edges == 2 ? 1 : 2
        );
      } else if (edges != 0) {
        Serial.println(
          "CHECK SENSORS: multiple holes triggered together."
        );
      } else if (missEvent == 1) {
        handleThrow(-1);
      }
    }
  }

  updateBuzzer();

  if (displayDirty ||
      uint32_t(millis() - lastDisplayAt) >= 100) {
    drawScreen();
  }
}