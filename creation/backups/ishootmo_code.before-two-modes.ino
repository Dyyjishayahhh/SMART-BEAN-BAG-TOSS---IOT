#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// =====================================================
// I-SHOOT MO - SMART BEANBAG TOSS
// Board: WEMOS LOLIN32 / ESP32-WROOM
// Display: 1.3-inch SH1106 128x64 I2C
// =====================================================

// ---------------- OLED ----------------

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ---------------- SENSORS ----------------

// Order: 10 points, 20 points, 30 points
const byte SENSOR_PINS[3] = {34, 35, 32};
const int ZONE_POINTS[3] = {10, 20, 30};

// ---------------- LEDs ----------------

// Blue = 10, Green = 20, Red = 30
const byte LED_PINS[3] = {25, 26, 27};

// ---------------- BUTTONS ----------------

const byte START_MODE_PIN = 18;
const byte RESET_PIN = 19;
const byte MISS_PIN = 23;

// ---------------- BUZZER ----------------

// Low-trigger, 3-pin active buzzer module
const byte BUZZER_PIN = 33;

// ---------------- SETTINGS ----------------

const int FIXED_THROWS = 6;

const unsigned long TIMED_ROUND_MS = 60000;
const unsigned long SENSOR_LOCKOUT_MS = 650;
const unsigned long BUTTON_DEBOUNCE_MS = 45;
const unsigned long HOLD_TO_START_MS = 900;

// ---------------- GAME VARIABLES ----------------

enum GameMode {
  FIXED_MODE,
  TIMED_MODE
};

GameMode gameMode = FIXED_MODE;

bool gameRunning = false;
bool finalScreen = false;

int score = 0;
int successes = 0;
int misses = 0;
int throwsUsed = 0;

int zoneHits[3] = {0, 0, 0};

unsigned long roundStartedAt = 0;
unsigned long lastScoreAt = 0;
unsigned long lastDisplayAt = 0;

bool lastSensorState[3] = {
  HIGH,
  HIGH,
  HIGH
};

// LED and buzzer feedback
int feedbackLed = -1;
unsigned long feedbackEndsAt = 0;

// ---------------- BUTTON STRUCTURE ----------------

struct ButtonState {
  byte pin;
  bool stable;
  bool lastReading;
  unsigned long changedAt;
  unsigned long pressedAt;
  bool holdHandled;
};

ButtonState startButton = {
  START_MODE_PIN, HIGH, HIGH, 0, 0, false
};

ButtonState resetButton = {
  RESET_PIN, HIGH, HIGH, 0, 0, false
};

ButtonState missButton = {
  MISS_PIN, HIGH, HIGH, 0, 0, false
};

// =====================================================
// BUZZER
// =====================================================

void buzzerOn() {
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerOff() {
  digitalWrite(BUZZER_PIN, HIGH);
}

// =====================================================
// LED AND BUZZER FEEDBACK
// =====================================================

void startFeedback(int zone) {
  if (feedbackLed >= 0) {
    digitalWrite(LED_PINS[feedbackLed], LOW);
  }

  feedbackLed = zone;

  digitalWrite(LED_PINS[zone], HIGH);
  buzzerOn();

  feedbackEndsAt = millis() + 180;
}

void updateFeedback() {
  if (
    feedbackLed >= 0 &&
    (long)(millis() - feedbackEndsAt) >= 0
  ) {
    digitalWrite(LED_PINS[feedbackLed], LOW);
    feedbackLed = -1;
    buzzerOff();
  }
}

// =====================================================
// RESET GAME
// =====================================================

void resetSession() {
  gameRunning = false;
  finalScreen = false;

  score = 0;
  successes = 0;
  misses = 0;
  throwsUsed = 0;

  for (int i = 0; i < 3; i++) {
    zoneHits[i] = 0;

    digitalWrite(LED_PINS[i], LOW);

    lastSensorState[i] =
      digitalRead(SENSOR_PINS[i]);
  }

  feedbackLed = -1;
  buzzerOff();

  Serial.println("Game reset");
}

// =====================================================
// START GAME
// =====================================================

void beginGame() {
  resetSession();

  gameRunning = true;
  roundStartedAt = millis();

  // Allows an immediate first score
  lastScoreAt = millis() - SENSOR_LOCKOUT_MS;

  buzzerOn();
  delay(80);
  buzzerOff();

  Serial.println("Game started");
}

// =====================================================
// END GAME
// =====================================================

void endGame() {
  gameRunning = false;
  finalScreen = true;

  for (int i = 0; i < 2; i++) {
    buzzerOn();
    delay(120);
    buzzerOff();
    delay(90);
  }

  Serial.println("Game finished");
}

// =====================================================
// COUNT SCORE
// =====================================================

void countZone(int zone) {
  if (!gameRunning) {
    return;
  }

  unsigned long now = millis();

  if (now - lastScoreAt < SENSOR_LOCKOUT_MS) {
    return;
  }

  lastScoreAt = now;

  score += ZONE_POINTS[zone];
  successes++;
  throwsUsed++;
  zoneHits[zone]++;

  Serial.print("Scored: ");
  Serial.print(ZONE_POINTS[zone]);
  Serial.print(" | Total score: ");
  Serial.println(score);

  startFeedback(zone);

  if (
    gameMode == FIXED_MODE &&
    throwsUsed >= FIXED_THROWS
  ) {
    endGame();
  }
}

// =====================================================
// COUNT MISS
// =====================================================

void countMiss() {
  if (!gameRunning) {
    return;
  }

  misses++;
  throwsUsed++;

  Serial.print("Miss counted | Total misses: ");
  Serial.println(misses);

  buzzerOn();
  delay(35);
  buzzerOff();

  if (
    gameMode == FIXED_MODE &&
    throwsUsed >= FIXED_THROWS
  ) {
    endGame();
  }
}

// =====================================================
// BUTTON READING
// =====================================================

// Returned events:
// 0 = nothing
// 1 = pressed
// 2 = released
// 3 = long press

int readButton(
  ButtonState &button,
  bool allowHold
) {
  bool reading = digitalRead(button.pin);
  unsigned long now = millis();

  if (reading != button.lastReading) {
    button. dAt = now;
    button.lastReading = reading;
  }

  int event = 0;

  if (
    now - button.changedAt >= BUTTON_DEBOUNCE_MS &&
    reading != button.stable
  ) {
    button.stable = reading;

    if (button.stable == LOW) {
      button.pressedAt = now;
      button.holdHandled = false;
      event = 1;
    } else {
      event = 2;
    }
  }

  if (
    allowHold &&
    button.stable == LOW &&
    !button.holdHandled &&
    now - button.pressedAt >= HOLD_TO_START_MS
  ) {
    button.holdHandled = true;
    event = 3;
  }

  return event;
}

// =====================================================
// FIND MOST HIT ZONE
// =====================================================

int topZone() {
  int best = 0;

  for (int i = 1; i < 3; i++) {
    if (zoneHits[i] > zoneHits[best]) {
      best = i;
    }
  }

  if (successes == 0) {
    return -1;
  }

  return best;
}

// =====================================================
// OLED IDLE SCREEN
// =====================================================

void drawIdle() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("I-SHOOT MO, I-SHOOT MO!!");

  display.setCursor(0, 18);
  display.print("Mode: ");

  if (gameMode == FIXED_MODE) {
    display.println("6 THROWS");
  } else {
    display.println("60 SECONDS");
  }

  display.setCursor(0, 36);
  display.println("Tap: change mode");

  display.setCursor(0, 50);
  display.println("Hold: start game");

  display.display();
}

// =====================================================
// OLED RUNNING SCREEN
// =====================================================

void drawRunning() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);

  display.print("S:");
  display.println(score);

  display.setTextSize(1);
  display.setCursor(0, 28);

  display.print("Hits:");
  display.print(successes);

  display.print(" Miss:");
  display.println(misses);

  display.setCursor(0, 44);

  if (gameMode == FIXED_MODE) {
    display.print("Throws left: ");
    display.println(FIXED_THROWS - throwsUsed);
  } else {
    unsigned long elapsed =
      millis() - roundStartedAt;

    int secondsLeft;

    if (elapsed >= TIMED_ROUND_MS) {
      secondsLeft = 0;
    } else {
      secondsLeft =
        (TIMED_ROUND_MS - elapsed + 999) / 1000;
    }

    display.print("Time left: ");
    display.print(secondsLeft);
    display.println("s");
  }

  display.display();
}

// =====================================================
// OLED FINAL SCREEN
// =====================================================

void drawFinal() {
  int best = topZone();

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("FINAL RESULT");

  display.setTextSize(2);
  display.setCursor(0, 14);

  display.print("S:");
  display.println(score);

  display.setTextSize(1);
  display.setCursor(0, 38);

  display.print("Hit:");
  display.print(successes);

  display.print(" Miss:");
  display.println(misses);

  display.setCursor(0, 52);
  display.print("Top zone: ");

  if (best < 0) {
    display.println("None");
  } else {
    display.println(ZONE_POINTS[best]);
  }

  display.display();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  // ESP32 I2C:
  // SDA = GPIO21
  // SCL = GPIO22
  Wire.begin(21, 22);

  for (int i = 0; i < 3; i++) {
    pinMode(SENSOR_PINS[i], INPUT);
    pinMode(LED_PINS[i], OUTPUT);

    digitalWrite(LED_PINS[i], LOW);

    lastSensorState[i] =
      digitalRead(SENSOR_PINS[i]);
  }

  // Buttons connect between GPIO and GND
  pinMode(START_MODE_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(MISS_PIN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  delay(250);

  // Start SH1106 OLED
  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println(
      "SH1106 OLED not found."
    );

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  Serial.println("System ready");

  drawIdle();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  unsigned long now = millis();

  updateFeedback();

  int startEvent =
    readButton(startButton, !gameRunning);

  int resetEvent =
    readButton(resetButton, false);

  int missEvent =
    readButton(missButton, false);

  // RESET button
  if (resetEvent == 1) {
    resetSession();
  }

  // START/MODE button
  if (!gameRunning && !finalScreen) {
    // Hold to start the selected mode
    if (startEvent == 3) {
      beginGame();
    }

    // Tap and release to change mode
    else if (
      startEvent == 2 &&
      !startButton.holdHandled
    ) {
      if (gameMode == FIXED_MODE) {
        gameMode = TIMED_MODE;
      } else {
        gameMode = FIXED_MODE;
      }

      Serial.print("Mode: ");

      if (gameMode == FIXED_MODE) {
        Serial.println("6 THROWS");
      } else {
        Serial.println("60 SECONDS");
      }
    }
  }

  // START on final screen returns to idle
  else if (
    finalScreen &&
    startEvent == 1
  ) {
    // Prevent release from changing mode
    startButton.holdHandled = true;
    resetSession();
  }

  // MISS button
  if (missEvent == 1) {
    countMiss();
  }

  // Sensors work only during the game
  if (gameRunning) {
    for (int i = 0; i < 3; i++) {
      bool sensorState =
        digitalRead(SENSOR_PINS[i]);

      // Count only when OUT changes HIGH to LOW
      if (
        sensorState == LOW &&
        lastSensorState[i] == HIGH
      ) {
        countZone(i);
      }

      lastSensorState[i] = sensorState;
    }

    if (
      gameMode == TIMED_MODE &&
      now - roundStartedAt >= TIMED_ROUND_MS
    ) {
      endGame();
    }
  }

  // OLED refresh
  if (now - lastDisplayAt >= 100) {
    lastDisplayAt = now;

    if (gameRunning) {
      drawRunning();
    } else if (finalScreen) {
      drawFinal();
    } else {
      drawIdle();
    }
  }
}