#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <esp_random.h>

// I-SHOOT MO | WEMOS LOLIN32 / ESP32-WROOM
// Confirmed display: SH1106, 128 x 64 I2C.
#include <Adafruit_SH110X.h>

#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(128, 64, &Wire, -1);
const uint16_t OLED_WHITE = SH110X_WHITE;

// Array order: 10-point, 20-point, 30-point hole.
const byte SENSOR_PINS[3] = {34, 35, 32};
const byte LED_PINS[3] = {25, 26, 27};
const int ZONE_POINTS[3] = {10, 20, 30};

const byte START_MODE_PIN = 18;
const byte RESET_PIN = 19;
const byte MISS_PIN = 23;

// Three-pin ACTIVE buzzer module: signal on GPIO 33.
const byte BUZZER_PIN = 33;
// Test HIGH-trigger operation first. Set true for a LOW-trigger module.
constexpr bool BUZZER_ACTIVE_LOW = false;
constexpr byte BUZZER_ON_LEVEL = BUZZER_ACTIVE_LOW ? LOW : HIGH;
constexpr byte BUZZER_OFF_LEVEL = BUZZER_ACTIVE_LOW ? HIGH : LOW;

constexpr int FIXED_THROWS = 6;
constexpr uint32_t TIMED_ROUND_MS = 60000;
constexpr uint32_t SENSOR_LOCKOUT_MS = 650;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 45;
constexpr uint32_t HOLD_TO_START_MS = 900;

// Keep unchanged for every player in the same competition.
constexpr uint32_t CHASE_SEQUENCE_SEED = 20261113;

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
    running = finished = false;
    score = hits = wrong = misses = throwsUsed = 0;

    activeZone = -1;
    orderLength = nextIndex = 0;
    startedAt = lastThrowAt = 0;

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

  // -1 = manual MISS; 0..2 = a hole sensor.
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
      // Precision advances after every recorded throw.
      // Chase advances only after a correct hit.
      selectNextTarget();
    }

    return result;
  }
};

GameState game;

bool lastSensorState[3] = {HIGH, HIGH, HIGH};
bool buzzerActive = false;
bool displayDirty = true;

uint32_t buzzerStartedAt = 0;
uint32_t buzzerDuration = 0;
uint32_t lastDisplayAt = 0;

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

void beep(uint32_t duration) {
  digitalWrite(BUZZER_PIN, BUZZER_ON_LEVEL);
  buzzerActive = true;
  buzzerStartedAt = millis();
  buzzerDuration = duration;
}

void updateBuzzer() {
  if (buzzerActive &&
      uint32_t(millis() - buzzerStartedAt) >= buzzerDuration) {
    digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
    buzzerActive = false;
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
  Serial.print("TARGET,points=");
  Serial.println(ZONE_POINTS[game.activeZone]);
}

void resetSession() {
  game.clear();
  updateTargetLeds();

  buzzerActive = false;
  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);

  displayDirty = true;
  Serial.println("RESET");
}

void beginGame() {
  uint32_t seed = game.mode == PRECISION
                    ? esp_random()
                    : CHASE_SEQUENCE_SEED;

  game.start(game.mode, millis(), seed);

  updateTargetLeds();
  beep(80);
  displayDirty = true;

  Serial.print("START,mode=");
  Serial.println(
    game.mode == PRECISION ? "PRECISION" : "TARGET_CHASE"
  );

  printTarget();
}

void announceFinish() {
  updateTargetLeds();
  beep(400);
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
  }

  if (wasRunning && game.finished) {
    announceFinish();
  } else if (result != IGNORED) {
    updateTargetLeds();
    beep(result == CORRECT_HIT ? 160 : 50);
    printTarget();
  }
}

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

// Sample continuously so old detections are not replayed.
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

  // Ties show the lowest-valued tied zone.
  return best;
}

void drawScreen() {
  display.clearDisplay();
  display.setTextColor(OLED_WHITE);
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

      uint32_t left = elapsed >= TIMED_ROUND_MS
                        ? 0
                        : TIMED_ROUND_MS - elapsed;

      display.print("Time: ");
      display.print((left + 999) / 1000);
      display.println("s");
    }
  } else {
    display.println("FINAL RESULT");

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

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  for (int i = 0; i < 3; ++i) {
    pinMode(SENSOR_PINS[i], INPUT);
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);

    lastSensorState[i] = digitalRead(SENSOR_PINS[i]);
  }

  pinMode(START_MODE_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(MISS_PIN, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.beginTransmission(OLED_ADDRESS);
  bool oledPresent = Wire.endTransmission() == 0;

  bool oledReady =
    oledPresent && display.begin(OLED_ADDRESS, true);

  if (!oledReady) {
    Serial.println(
      "OLED error: check model/address, SDA21/SCL22, power."
    );

    while (true) {
      delay(1000);
    }
  }

  Serial.println(
    "READY: tap START to select; hold 0.9s to play."
  );

  drawScreen();
}

void loop() {
  updateBuzzer();

  byte edges = pollSensors();

  int startEvent = readButton(startButton, !game.running);
  int resetEvent = readButton(resetButton, false);
  int missEvent = readButton(missButton, false);

  if (resetEvent == 1) {
    startButton.holdHandled = true;
    resetSession();
  } else if (!game.running) {
    if (game.finished && startEvent == 1) {
      startButton.holdHandled = true;
      resetSession();
    } else if (!game.finished && startEvent == 3) {
      beginGame();
    } else if (!game.finished &&
               startEvent == 2 &&
               !startButton.holdHandled) {
      game.mode = game.mode == PRECISION
                    ? TARGET_CHASE
                    : PRECISION;

      displayDirty = true;
    }
  } else if (game.expire(millis())) {
    announceFinish();
  } else if (edges == 1 || edges == 2 || edges == 4) {
    handleThrow(edges == 1 ? 0 : edges == 2 ? 1 : 2);
  } else if (edges != 0) {
    Serial.println(
      "CHECK SENSORS: multiple holes triggered together."
    );
  } else if (missEvent == 1) {
    handleThrow(-1);
  }

  if (displayDirty ||
      uint32_t(millis() - lastDisplayAt) >= 100) {
    drawScreen();
  }
}
