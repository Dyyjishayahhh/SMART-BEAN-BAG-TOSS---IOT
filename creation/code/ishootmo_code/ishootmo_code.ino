#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp_random.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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

constexpr uint8_t OLED_ADDRESSES[] = {0x3C, 0x3D};
constexpr int OLED_SDA_PIN = 21;
constexpr int OLED_SCL_PIN = 22;
constexpr uint32_t OLED_BUS_HZ = 100000;
constexpr uint32_t OLED_RETRY_MS = 3000;

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
constexpr uint32_t SENSOR_DEBOUNCE_MS = 5;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 45;
constexpr uint32_t HOLD_TO_START_MS = 900;
constexpr uint8_t MEMORY_MAX_LEVEL = 10;
constexpr int MEMORY_MAX_THROWS = MEMORY_MAX_LEVEL + 2;
constexpr uint32_t MEMORY_PREVIEW_LEAD_MS = 600;

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
  TARGET_CHASE,
  COLOR_MEMORY
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

  int order[MEMORY_MAX_THROWS] = {};
  int orderLength = 0;
  int nextIndex = 0;

  uint8_t practiceLevel = 0;
  uint8_t memoryLevel = 1;
  bool previewing = false;
  uint32_t roundDuration = TIMED_ROUND_MS;
  uint32_t startedAt = 0;
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
    practiceLevel = 0;
    memoryLevel = 1;
    previewing = false;
    roundDuration = TIMED_ROUND_MS;
    startedAt = 0;

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

    if (mode == PRECISION && practiceLevel == 1) {
      order[3] = order[5] = 0;
    } else if (mode == PRECISION && practiceLevel == 3) {
      order[1] = order[3] = 2;
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
    uint32_t seed,
    uint8_t level = 0
  ) {
    clear();

    mode = selected;
    practiceLevel = mode == COLOR_MEMORY ? 0 : level;
    if (mode == TARGET_CHASE) {
      roundDuration = level == 1 ? 75000 : level == 3 ? 45000 : TIMED_ROUND_MS;
    }
    randomState = seed ? seed : 1;

    if (mode == COLOR_MEMORY) {
      memoryLevel = level >= 1 && level <= MEMORY_MAX_LEVEL ? level : 1;
      orderLength = memoryLevel + 2;
      const int first = randomBelow(3);
      order[0] = first;
      for (int i = 1; i < orderLength; ++i) {
        order[i] = (order[i - 1] + 1 + randomBelow(2)) % 3;
      }
      previewing = true;
    } else {
      shuffleTargets();
    }
    selectNextTarget();

    startedAt = now;

    running = true;
  }

  constexpr uint32_t memoryFlashMs() const { return 900 - 55 * (memoryLevel - 1); }
  constexpr uint32_t memoryStepMs() const { return memoryFlashMs() + 300 - 15 * (memoryLevel - 1); }

  constexpr int memoryLight(uint32_t now) const {
    const uint32_t elapsed = now - startedAt;
    if (!previewing || elapsed < MEMORY_PREVIEW_LEAD_MS) return -1;
    const uint32_t timeInSequence = elapsed - MEMORY_PREVIEW_LEAD_MS;
    const uint32_t step = timeInSequence / memoryStepMs();
    return step < uint32_t(orderLength) && timeInSequence % memoryStepMs() < memoryFlashMs() ? order[step] : -1;
  }

  constexpr bool endPreview(uint32_t now) {
    if (!previewing || now - startedAt < MEMORY_PREVIEW_LEAD_MS + uint32_t(orderLength) * memoryStepMs()) return false;
    previewing = false;
    startedAt = now;
    return true;
  }

  constexpr bool memoryCleared() const { return finished && mode == COLOR_MEMORY && hits == orderLength; }

  constexpr uint8_t nextMemoryLevel() const {
    if (!memoryCleared()) return memoryLevel;
    return memoryLevel == MEMORY_MAX_LEVEL ? 1 : memoryLevel + 1;
  }

  constexpr void finish() {
    running = false;
    finished = true;
    activeZone = -1;
  }

  constexpr bool expire(uint32_t now) {
    if (running &&
        mode == TARGET_CHASE &&
        uint32_t(now - startedAt) >= roundDuration) {
      finish();
      return true;
    }

    return false;
  }

  constexpr bool markHitAsMiss(int zone) {
    if ((!running && !finished) || previewing || zone < 0 || zone > 2 || zoneHits[zone] == 0) return false;
    score -= (zone + 1) * 10;
    --hits;
    --zoneHits[zone];
    ++misses;
    return true;
  }

  // -1 = manual MISS
  //  0 = 10-point sensor
  //  1 = 20-point sensor
  //  2 = 30-point sensor
  constexpr ThrowResult recordThrow(int zone, uint32_t now) {
    if (!running || previewing || zone < -1 || zone > 2 || expire(now)) {
      return IGNORED;
    }

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

    if (mode != TARGET_CHASE && throwsUsed >= orderLength) {
      finish();
    } else if (mode != TARGET_CHASE || result == CORRECT_HIT) {
      // Precision advances after every recorded attempt.
      // Chase advances only after a correct hit.
      selectNextTarget();
    }

    return result;
  }
};

GameState game;
bool memoryStartedOnWeb = false;
uint8_t versusSlot = 0;

#include "online.h"

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

  constexpr int takeCue(uint32_t elapsed, uint32_t duration = TIMED_ROUND_MS) {
    uint32_t remaining = elapsed >= duration ? 0 : duration - elapsed;

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

struct TargetInput {
  bool rawBlocked = false;
  bool stableBlocked = false;
  uint32_t changedAt = 0;

  constexpr bool sample(bool blocked, uint32_t now) {
    if (blocked != rawBlocked) {
      rawBlocked = blocked;
      changedAt = now;
    }
    if (rawBlocked == stableBlocked || now - changedAt < SENSOR_DEBOUNCE_MS) return false;
    stableBlocked = rawBlocked;
    return stableBlocked;
  }
};

TargetInput targetInputs[3];
struct SensorHit {
  uint32_t at;
  uint8_t holes;
};
constexpr unsigned SENSOR_QUEUE_SIZE = 24;
StaticQueue_t sensorQueueStorage;
uint8_t sensorQueueBytes[SENSOR_QUEUE_SIZE * sizeof(SensorHit)];
QueueHandle_t sensorQueue;
bool sensorsSampleInLoop = false;

bool displayDirty = true;
uint32_t lastDisplayAt = 0;
bool oledAvailable = false;
uint32_t lastOledCheckAt = 0;

// ---------------- GAME FUNCTIONS ----------------

void updateCountdownSound() {
  if (!game.running || game.mode != TARGET_CHASE) {
    return;
  }

  int second = countdown.takeCue(
    uint32_t(millis() - game.startedAt), game.roundDuration
  );

  if (second >= 0) {
    beep(COUNTDOWN_BEEP_MS);

    Serial.print("COUNTDOWN,");
    Serial.println(second);

    displayDirty = true;
  }
}

void updateTargetLeds() {
  const int lit = game.mode == COLOR_MEMORY ? game.memoryLight(millis()) : game.activeZone;
  for (int i = 0; i < 3; ++i) {
    digitalWrite(
      LED_PINS[i], game.running && lit == i ? HIGH : LOW
    );
  }
}

void printTarget() {
  if (!game.running || game.mode == COLOR_MEMORY || game.activeZone < 0) {
    return;
  }

  Serial.print("TARGET,points=");
  Serial.println(ZONE_POINTS[game.activeZone]);
}

void resetSession() {
  // Keep the selected mode.
  #ifdef ISHOOT_ONLINE
  online::clearRound();
  #endif
  game.clear();
  versusSlot = 0;
  memoryStartedOnWeb = false;

  updateTargetLeds();
  stopSounds();

  countdown.lastSecond = -1;
  displayDirty = true;

  Serial.println("RESET");
}

void beginGame(const char *player, uint8_t level, const char *startId, bool memoryPractice, uint8_t slot = 0) {
  uint32_t seed = game.mode != TARGET_CHASE || level
                    ? esp_random()
                    : CHASE_SEQUENCE_SEED;

  game.start(game.mode, millis(), seed, level);
  versusSlot = slot;
  memoryStartedOnWeb = game.mode == COLOR_MEMORY && startId[0] != '\0';
  #ifdef ISHOOT_ONLINE
  online::startRound(game, player, startId, memoryPractice);
  #endif

  countdown.lastSecond = -1;

  updateTargetLeds();
  beep(GAME_START_BEEP_MS);

  displayDirty = true;

  Serial.print("START,mode=");
  Serial.println(
    game.mode == PRECISION ? "PRECISION" : game.mode == TARGET_CHASE ? "TARGET_CHASE" : "COLOR_MEMORY"
  );

  printTarget();
}

void announceFinish() {
  #ifdef ISHOOT_ONLINE
  online::closeRound(online::Status::Complete);
  #endif
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

ThrowResult handleThrow(int zone, uint32_t now) {
  bool wasRunning = game.running;
  #ifdef ISHOOT_ONLINE
  const int aim = game.activeZone;
  #endif
  ThrowResult result = game.recordThrow(zone, now);

  if (result != IGNORED) {
    #ifdef ISHOOT_ONLINE
    online::record(aim, zone, now - game.startedAt);
    #endif
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
  return result;
}

#ifdef ISHOOT_ONLINE
void processWebCommand() {
  online::Command command;
  if (!online::takeCommand(command)) return;
  const char *result = "stale";
  if (millis() - command.receivedAt > 3000) {
    result = "expired";
  } else if (command.kind == online::Kind::Start && !game.running
             && (!command.roundId[0] || (game.finished && game.mode == COLOR_MEMORY
                 && online::matches(command) && command.level == game.nextMemoryLevel()))) {
    game.mode = command.mode;
    beginGame(command.player, command.level, command.id, command.practice, command.slot);
    result = "applied";
  } else if (command.kind == online::Kind::Reset && online::matches(command)) {
    startButton.holdHandled = true;
    resetSession();
    beep(BUTTON_BEEP_MS);
    result = "applied";
  } else if (command.kind == online::Kind::Correct && online::matches(command) && game.throwsUsed == command.count) {
    if (online::correct(game, command)) {
      Serial.printf("CORRECTED,score=%d,throws=%d\n", game.score, game.throwsUsed);
      displayDirty = true;
      result = "applied";
    }
  } else if (command.kind == online::Kind::Miss && game.running && !game.previewing
          && online::matches(command) && game.throwsUsed == command.count) {
    if (handleThrow(-1, millis()) == MISSED_THROW) {
      beep(BUTTON_BEEP_MS);
      result = "applied";
    } else if (game.running) {
      result = "wait";
    }
  }
  online::acknowledge(game, command, result);
}
#endif

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

void queueSensorHits() {
  const auto now = millis();
  uint8_t holes = 0;
  for (int i = 0; i < 3; ++i) {
    const bool blocked = digitalRead(SENSOR_PINS[i]) == LOW;
    if (targetInputs[i].sample(blocked, now)) holes |= uint8_t(1 << i);
  }
  if (holes) {
    const SensorHit hit = {now, holes};
    xQueueSend(sensorQueue, &hit, 0);
  }
}

void sensorLoop(void *) {
  for (;;) {
    queueSensorHits();
    vTaskDelay(1);
  }
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

bool probeOled(int sda, int scl, bool report) {
  if (!Wire.begin(sda, scl, OLED_BUS_HZ)) {
    if (report) Serial.printf("OLED bus setup failed on SDA GPIO%d / SCL GPIO%d.\n", sda, scl);
    return false;
  }

  for (const uint8_t address : OLED_ADDRESSES) {
    Wire.beginTransmission(address);
    const uint8_t status = Wire.endTransmission();
    if (status == 0 && display.begin(address, true)) {
      Serial.printf("OLED connected at 0x%02X on SDA GPIO%d / SCL GPIO%d.\n", address, sda, scl);
      return true;
    }
    if (report) {
      if (status == 0) {
        Serial.printf("OLED at 0x%02X acknowledged, but initialization failed.\n", address);
      } else {
        Serial.printf("OLED probe SDA GPIO%d / SCL GPIO%d at 0x%02X returned I2C status %u.\n",
                      sda, scl, address, status);
      }
    }
  }
  return false;
}

bool connectOled(bool report = false) {
  lastOledCheckAt = millis();
  if (probeOled(OLED_SDA_PIN, OLED_SCL_PIN, report)) return true;
  Wire.end();

  // The display connector may have its two signal wires in the opposite order.
  if (probeOled(OLED_SCL_PIN, OLED_SDA_PIN, report)) return true;
  Wire.end();

  if (report) {
    pinMode(OLED_SDA_PIN, INPUT_PULLUP);
    pinMode(OLED_SCL_PIN, INPUT_PULLUP);
    delayMicroseconds(100);
    Serial.printf("OLED bus idle levels GPIO%d=%d GPIO%d=%d.\n",
                  OLED_SDA_PIN, digitalRead(OLED_SDA_PIN), OLED_SCL_PIN, digitalRead(OLED_SCL_PIN));
  }
  return false;
}

void drawScreen() {
  if (!oledAvailable) return;

  display.clearDisplay();

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (!game.running && !game.finished) {
    display.setTextSize(2);
    display.println("Ready!");
    display.setTextSize(1);
    display.setCursor(0, 19);
    display.println("Press Start");

    display.setCursor(0, 34);
    display.println(
      game.mode == PRECISION
        ? "1 PRECISION"
        : game.mode == TARGET_CHASE ? "2 TARGET CHASE" : "3 COLOR MEMORY"
    );

    display.setCursor(0, 56);
    display.println("Tap: mode  Hold: play");
  } else if (game.previewing) {
    display.println("COLOR MEMORY");
    display.setCursor(0, 20);
    display.println("Watch the lights!");
    display.setCursor(0, 38);
    display.print("Level ");
    display.print(game.memoryLevel);
    display.print(" / ");
    display.print(game.orderLength);
    display.println(" colors");
  } else if (game.running) {
    display.setTextSize(2);
    display.print("S:");
    display.println(game.score);

    display.setTextSize(1);
    display.setCursor(0, 20);

    if (versusSlot) {
      display.print("P");
      display.print(versusSlot);
      display.print(" THROW / AIM ");
      display.println(ZONE_POINTS[game.activeZone]);
    } else if (game.mode == COLOR_MEMORY) {
      display.println("Throw in order");
    } else {
      display.print("Aim: ");
      display.print(ZONE_POINTS[game.activeZone]);
      display.println(" pts");
    }

    display.setCursor(0, 32);

    display.print("Hit:");
    display.print(game.hits);

    display.print(" Wrong:");
    display.println(game.wrong);

    display.setCursor(0, 44);

    display.print("Miss:");
    display.println(game.misses);

    display.setCursor(0, 56);

    if (game.mode != TARGET_CHASE) {
      display.print("Throws left: ");
      display.println(game.orderLength - game.throwsUsed);
    } else {
      uint32_t elapsed = uint32_t(millis() - game.startedAt);

      uint32_t remaining = elapsed >= game.roundDuration
                             ? 0
                             : game.roundDuration - elapsed;

      display.print("Time: ");
      display.print((remaining + 999) / 1000);
      display.println("s");
    }
  } else {
    if (game.mode == COLOR_MEMORY) {
      display.println(game.memoryCleared() ? "LEVEL CLEARED" : "TRY THIS LEVEL AGAIN");
    } else {
      display.println(game.mode == TARGET_CHASE ? "TIME UP - 0s" : "FINAL RESULT");
    }

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
    if (game.mode == COLOR_MEMORY) {
      if (memoryStartedOnWeb) display.println("Use app to continue");
      else {
        display.print("Hold: Level ");
        display.println(game.nextMemoryLevel());
      }
    } else {
      display.print("Top zone: ");
      const int best = topZone();
      if (best < 0) display.println("None");
      else display.println(ZONE_POINTS[best]);
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

  for (int i = 0; i < 3; ++i) {
    pinMode(SENSOR_PINS[i], INPUT);

    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);

    const bool blocked = digitalRead(SENSOR_PINS[i]) == LOW;
    targetInputs[i] = {blocked, blocked, millis()};
  }
  sensorQueue = xQueueCreateStatic(SENSOR_QUEUE_SIZE, sizeof(SensorHit), sensorQueueBytes, &sensorQueueStorage);
  sensorsSampleInLoop = xTaskCreate(sensorLoop, "ishoot-sensors", 2048, nullptr, 2, nullptr) != pdPASS;

  // Buttons connect between their GPIO and GND.
  pinMode(START_MODE_PIN, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(MISS_PIN, INPUT_PULLUP);

  oledAvailable = connectOled(true);
  if (!oledAvailable) {
    Serial.println("OLED unavailable. Game controls are available while the display reconnects.");
  }

  Serial.println("I-SHOOT MO / SH1106 / LOW-TRIGGER BUZZER");

  Serial.println(
    "READY: tap START to select; hold 0.9s to play."
  );

  drawScreen();

  // Short startup beep, then silence.
  beep(STARTUP_BEEP_MS);
  #ifdef ISHOOT_ONLINE
  online::begin(game);
  #endif
}

// ---------------- MAIN LOOP ----------------

void loop() {
  updateBuzzer();

  if (sensorsSampleInLoop) queueSensorHits();

  int startEvent = readButton(startButton, !game.running);
  int resetEvent = readButton(resetButton, false);
  int missEvent = readButton(missButton, false);
  if (resetEvent == 1 || !game.running || game.previewing) xQueueReset(sensorQueue);

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
      if (game.finished && game.mode == COLOR_MEMORY && !memoryStartedOnWeb) {
        if (startEvent == 3) beginGame("Guest", game.nextMemoryLevel(), "", false);
        else if (startEvent == 2 && !startButton.holdHandled) resetSession();
      } else if (game.finished && startEvent == 1) {
        // Return from the result screen to the menu.
        startButton.holdHandled = true;

        resetSession();
        beep(BUTTON_BEEP_MS);
      } else if (!game.finished && startEvent == 3) {
        // Hold START/MODE to start.
        beginGame("Guest", 0, "", false);
      } else if (!game.finished &&
                 startEvent == 2 &&
                 !startButton.holdHandled) {
        // Tap START/MODE to change modes.
        game.mode = game.mode == PRECISION ? TARGET_CHASE
          : game.mode == TARGET_CHASE ? COLOR_MEMORY : PRECISION;

        // Long beep confirms the selected mode.
        beep(MODE_BEEP_MS);

        displayDirty = true;
      }
    } else if (game.previewing) {
      if (game.endPreview(millis())) {
        beep(GAME_START_BEEP_MS);
        displayDirty = true;
        Serial.println("MEMORY,throw");
      }
      updateTargetLeds();
    } else {
      // One beep per remaining second: 10, 9, ... 0.
      updateCountdownSound();

      SensorHit hit;
      bool sawHit = false;
      while (game.running && xQueueReceive(sensorQueue, &hit, 0) == pdTRUE) {
        if (int32_t(hit.at - game.startedAt) < 0) continue;
        sawHit = true;
        if (hit.holes == 1 || hit.holes == 2 || hit.holes == 4) {
          handleThrow(hit.holes == 1 ? 0 : hit.holes == 2 ? 1 : 2, hit.at);
        } else {
          Serial.println("CHECK SENSORS: multiple holes triggered together.");
        }
      }
      if (game.expire(millis())) {
        announceFinish();
      } else if (game.running && !sawHit && missEvent == 1) {
        handleThrow(-1, millis());
      }
    }
  }

  #ifdef ISHOOT_ONLINE
  for (int zone = 0; zone < 3; ++zone) online::observe(zone, digitalRead(SENSOR_PINS[zone]) == LOW, millis());
  processWebCommand();
  online::publish(game);
  #endif

  updateBuzzer();

  const uint32_t now = millis();
  if (!oledAvailable && now - lastOledCheckAt >= OLED_RETRY_MS) {
    oledAvailable = connectOled();
    if (oledAvailable) displayDirty = true;
  }
  if (oledAvailable && (displayDirty || now - lastDisplayAt >= 100)) {
    drawScreen();
  }
}
