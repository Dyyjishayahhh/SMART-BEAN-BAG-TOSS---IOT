#pragma once

// Board-local builds use the same game without network dependencies.
#if __has_include("online_config.h")
#include "online_config.h"
#define ISHOOT_ONLINE
#endif

#ifdef ISHOOT_ONLINE
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <cJSON.h>
#include <time.h>

namespace online {
constexpr uint16_t maxAttempts = 384;
constexpr uint16_t maxThrowCount = 15000;
constexpr uint32_t quickPulseMs = 35;
constexpr uint32_t pulseWindowMs = 6000;

enum class Kind : uint8_t { Start, Reset, Miss, Correct };
enum class Status : uint8_t { Active, Complete, Canceled };

struct Command {
  char id[37] = {};
  char roundId[17] = {};
  char player[25] = "Guest";
  Kind kind = Kind::Start;
  GameMode mode = PRECISION;
  uint8_t level = 0;
  uint16_t count = 0;
  uint16_t attempt = 0;
  uint8_t slot = 0;
  bool practice = false;
  uint32_t receivedAt = 0;
};

struct Attempt {
  uint32_t elapsed;
  uint8_t target;
  uint8_t landed;
};

struct Round {
  char id[17] = {};
  char startId[37] = {};
  char player[25] = "Guest";
  GameMode mode = PRECISION;
  uint8_t level = 0;
  uint16_t count = 0;
  uint16_t corrections = 0;
  bool practice = false;
  Status status = Status::Active;
  Attempt attempts[maxAttempts] = {};
};

struct Receipt {
  char id[37] = {};
  const char *result = "stale";
};

struct Sensor {
  bool low = false;
  uint32_t changedAt = 0;
  uint32_t pulses[6] = {};
  uint8_t next = 0;
  uint8_t size = 0;

  void update(bool blocked, uint32_t now) {
    if (blocked == low) return;
    if (low && now - changedAt < quickPulseMs) {
      pulses[next] = now;
      next = (next + 1) % 6;
      if (size < 6) ++size;
    }
    low = blocked;
    changedAt = now;
  }

  uint8_t pulseCount(uint32_t now) const {
    uint8_t count = 0;
    for (int i = 0; i < size; ++i) {
      if (now - pulses[i] <= pulseWindowMs) ++count;
    }
    return count;
  }
};

struct Snapshot {
  Round round;
  Receipt receipt;
  const char *phase = "ready";
  GameMode mode = PRECISION;
  uint32_t score = 0;
  uint16_t count = 0;
  uint8_t target = 0;
  uint32_t elapsed = 0;
  uint32_t duration = 0;
  uint32_t blockedMs[3] = {};
  uint8_t quickPulses[3] = {};
  bool dropped = false;
  bool previewing = false;
};

static Round current;
static Sensor sensors[3];
static Receipt lastReceipt;
static bool droppedRound = false;
static QueueHandle_t archived = nullptr;
static QueueHandle_t commands = nullptr;
static SemaphoreHandle_t snapshotLock = nullptr;
static TaskHandle_t syncTask = nullptr;
static Snapshot snapshot;
static uint32_t lastPublished = 0;

static const char *gameName(GameMode mode) {
  return mode == PRECISION ? "precision" : mode == TARGET_CHASE ? "chase" : "memory";
}

static const char *typeName(bool practice) {
  return practice ? "practice" : "competition";
}

static void startRound(const GameState &game, const char *player, const char *startId, bool memoryPractice) {
  current = Round{};
  snprintf(current.id, sizeof(current.id), "%08lx%08lx",
    static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  strlcpy(current.player, player, sizeof(current.player));
  strlcpy(current.startId, startId, sizeof(current.startId));
  current.mode = game.mode;
  current.level = game.mode == COLOR_MEMORY ? game.memoryLevel : game.practiceLevel;
  current.practice = game.mode == COLOR_MEMORY ? memoryPractice : game.practiceLevel != 0;
}

static void record(int aim, int landed, uint32_t elapsed) {
  if (!current.id[0] || current.count >= maxAttempts) return;
  current.attempts[current.count++] = {elapsed, uint8_t((aim + 1) * 10), uint8_t((landed + 1) * 10)};
}

static void closeRound(Status status) {
  if (!current.id[0] || current.status != Status::Active) return;
  current.status = status;
  if (archived && xQueueSend(archived, &current, 0) != pdTRUE) droppedRound = true;
}

static void clearRound() {
  closeRound(Status::Canceled);
  current = Round{};
}

static bool matches(const Command &command) {
  return current.id[0] && strcmp(current.id, command.roundId) == 0;
}

static bool correct(GameState &game, const Command &command) {
  if (!command.attempt || command.attempt > current.count) return false;
  auto &attempt = current.attempts[command.attempt - 1];
  if (!attempt.landed || attempt.landed != attempt.target || !game.markHitAsMiss(attempt.landed / 10 - 1)) return false;
  attempt.landed = 0;
  ++current.corrections;
  if (current.status == Status::Complete && xQueueSend(archived, &current, 0) != pdTRUE) droppedRound = true;
  return true;
}

static void observe(int index, bool blocked, uint32_t now) {
  sensors[index].update(blocked, now);
}

static void publish(const GameState &game, bool immediate = false) {
  if (!snapshotLock) return;
  const auto now = millis();
  const char *phase = game.running ? "playing" : game.finished ? "results" : "ready";
  const bool changed = strcmp(snapshot.phase, phase) != 0 || strcmp(snapshot.round.id, current.id) != 0
    || snapshot.count != game.throwsUsed || snapshot.round.corrections != current.corrections
    || snapshot.mode != game.mode || snapshot.previewing != game.previewing;
  if (!immediate && !changed && now - lastPublished < 100) return;
  lastPublished = now;
  xSemaphoreTake(snapshotLock, portMAX_DELAY);
  snapshot.round = current;
  snapshot.receipt = lastReceipt;
  snapshot.phase = phase;
  snapshot.mode = game.mode;
  snapshot.score = game.score;
  snapshot.count = game.throwsUsed;
  snapshot.target = game.mode == COLOR_MEMORY || game.activeZone < 0 ? 0 : (game.activeZone + 1) * 10;
  snapshot.previewing = game.previewing;
  snapshot.duration = game.mode == TARGET_CHASE ? game.roundDuration : 0;
  snapshot.elapsed = game.running ? now - game.startedAt : 0;
  snapshot.dropped = droppedRound;
  for (int i = 0; i < 3; ++i) {
    snapshot.blockedMs[i] = sensors[i].low ? now - sensors[i].changedAt : 0;
    snapshot.quickPulses[i] = sensors[i].pulseCount(now);
  }
  xSemaphoreGive(snapshotLock);
  if (syncTask && (immediate || changed)) xTaskNotifyGive(syncTask);
}

static bool takeCommand(Command &command) {
  return commands && xQueueReceive(commands, &command, 0) == pdTRUE;
}

static void acknowledge(const GameState &game, const Command &command, const char *result) {
  strlcpy(lastReceipt.id, command.id, sizeof(lastReceipt.id));
  lastReceipt.result = result;
  publish(game, true);
}

static void appendRound(String &out, const Round &round) {
  const char *status = round.status == Status::Active ? "active"
    : round.status == Status::Complete ? "complete" : "canceled";
  char data[240];
  snprintf(data, sizeof(data),
    "{\"id\":\"%s\",\"startId\":\"%s\",\"player\":\"%s\",\"game\":\"%s\",\"type\":\"%s\",\"level\":%u,\"status\":\"%s\",\"corrections\":%u,\"attempts\":[",
    round.id, round.startId, round.player, gameName(round.mode), typeName(round.practice), round.level, status, round.corrections);
  out += data;
  for (int i = 0; i < round.count; ++i) {
    const auto &attempt = round.attempts[i];
    snprintf(data, sizeof(data), "%s[%u,%u,%lu]", i ? "," : "",
      attempt.target, attempt.landed, static_cast<unsigned long>(attempt.elapsed));
    out += data;
  }
  out += "]}";
}

static String encode(const Snapshot &data, const Round *previous) {
  const auto &round = data.round;
  char text[660];
  snprintf(text, sizeof(text),
    "{\"board\":{\"version\":4,\"state\":{\"phase\":\"%s\",\"game\":\"%s\",\"type\":\"%s\",\"player\":\"%s\","
    "\"roundId\":\"%s\",\"startId\":\"%s\",\"level\":%u,\"score\":%u,\"count\":%u,\"elapsedMs\":%lu,\"durationMs\":%lu,\"target\":%u,\"previewing\":%s},"
    "\"sensors\":{\"blockedMs\":[%lu,%lu,%lu],\"quickPulses\":[%u,%u,%u]},\"dropped\":%s},\"rounds\":[",
    data.phase, gameName(data.mode), typeName(round.practice), round.player, round.id, round.startId, round.level,
    static_cast<unsigned>(data.score), data.count,
    static_cast<unsigned long>(data.elapsed), static_cast<unsigned long>(data.duration), data.target, data.previewing ? "true" : "false",
    static_cast<unsigned long>(data.blockedMs[0]), static_cast<unsigned long>(data.blockedMs[1]),
    static_cast<unsigned long>(data.blockedMs[2]), data.quickPulses[0], data.quickPulses[1], data.quickPulses[2],
    data.dropped ? "true" : "false");
  String out;
  out.reserve(800 + 24 * (round.count + (previous ? previous->count : 0)));
  out += text;
  const bool archivedOther = previous && strcmp(previous->id, round.id) != 0;
  if (archivedOther) appendRound(out, *previous);
  if (round.id[0]) {
    if (archivedOther) out += ',';
    appendRound(out, round);
  }
  if (data.receipt.id[0]) {
    snprintf(text, sizeof(text), "],\"ack\":{\"id\":\"%s\",\"result\":\"%s\"}}", data.receipt.id, data.receipt.result);
    out += text;
  } else {
    out += "],\"ack\":null}";
  }
  return out;
}

static const char *stringField(cJSON *object, const char *field) {
  const auto *item = cJSON_GetObjectItemCaseSensitive(object, field);
  return cJSON_IsString(item) ? item->valuestring : "";
}

static bool safePlayer(const char *player) {
  const size_t length = strlen(player);
  if (!length || length > 24) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char letter = player[i];
    if (!isalnum(letter) && letter != '-' && letter != '_' && letter != ' ') return false;
  }
  return true;
}

static const int integerField(cJSON *object, const char *field) {
  const auto *item = cJSON_GetObjectItemCaseSensitive(object, field);
  return cJSON_IsNumber(item) ? item->valueint : -1;
}

static bool acceptCommand(const String &body) {
  static char accepted[37] = {};
  bool queued = false;
  auto *response = cJSON_Parse(body.c_str());
  auto *message = cJSON_GetObjectItemCaseSensitive(response, "command");
  const char *id = stringField(message, "id");
  const char *kind = stringField(message, "kind");
  if (strlen(id) == 36 && strcmp(id, accepted) != 0) {
    Command request;
    request.receivedAt = millis();
    strlcpy(request.id, id, sizeof(request.id));
    bool valid = false;
    if (strcmp(kind, "start") == 0) {
      const auto *player = stringField(message, "player");
      const auto *mode = stringField(message, "game");
      const auto *roundId = stringField(message, "roundId");
      const int level = integerField(message, "level");
      const auto *slot = cJSON_GetObjectItemCaseSensitive(message, "slot");
      const bool memory = strcmp(mode, "memory") == 0;
      const bool chase = strcmp(mode, "chase") == 0;
      if (safePlayer(player) && (memory ? level >= 1 && level <= MEMORY_MAX_LEVEL : level >= 0 && level <= 3)
          && (!roundId[0] || (memory && strlen(roundId) == 16))
          && (memory || chase || strcmp(mode, "precision") == 0)) {
        valid = true;
        request.level = level;
        if (cJSON_IsNumber(slot) && (slot->valueint == 1 || slot->valueint == 2)) request.slot = slot->valueint;
        request.mode = memory ? COLOR_MEMORY : chase ? TARGET_CHASE : PRECISION;
        request.practice = strcmp(stringField(message, "type"), "practice") == 0;
        strlcpy(request.player, player, sizeof(request.player));
        strlcpy(request.roundId, roundId, sizeof(request.roundId));
      }
    } else if (strcmp(kind, "reset") == 0 || strcmp(kind, "miss") == 0 || strcmp(kind, "correct") == 0) {
      const auto *roundId = stringField(message, "roundId");
      const int count = integerField(message, "count");
      const int attempt = integerField(message, "attempt");
      const bool correction = strcmp(kind, "correct") == 0;
      if (strlen(roundId) == 16 && count >= 0 && count <= maxThrowCount
          && (!correction || (attempt > 0 && attempt <= maxAttempts))) {
        valid = true;
        request.kind = correction ? Kind::Correct : strcmp(kind, "reset") == 0 ? Kind::Reset : Kind::Miss;
        if (correction) request.attempt = attempt;
        request.count = count;
        strlcpy(request.roundId, roundId, sizeof(request.roundId));
      }
    }
    if (valid && xQueueSend(commands, &request, 0) == pdTRUE) {
      strlcpy(accepted, id, sizeof(accepted));
      queued = true;
    }
  }
  cJSON_Delete(response);
  return queued;
}

extern const uint8_t rootBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootBundleEnd[] asm("_binary_x509_crt_bundle_end");

static String fetchLiveSettings() {
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, String(ISHOOT_APP_URL) + "/api/game?action=device-live")) return {};
  http.addHeader("Authorization", String("Bearer ") + ISHOOT_DEVICE_TOKEN);
  String result;
  if (http.GET() == HTTP_CODE_OK) result = http.getString();
  http.end();
  return result;
}

static void commandLoop(void *) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    const String settings = fetchLiveSettings();
    auto *response = cJSON_Parse(settings.c_str());
    auto *live = cJSON_GetObjectItemCaseSensitive(response, "live");
    const String host = stringField(live, "host");
    const String key = stringField(live, "key");
    const String topic = stringField(live, "topic");
    cJSON_Delete(response);
    if (host.isEmpty() || topic.isEmpty() || !key.startsWith("sb_publishable_")) {
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }

    const String path = "/realtime/v1/websocket?vsn=1.0.0&apikey=" + key;
    const String channel = "realtime:" + topic;
    const String join = "{\"topic\":\"" + channel + "\",\"event\":\"phx_join\",\"payload\":"
      "{\"config\":{\"private\":true,\"broadcast\":{\"ack\":false,\"self\":false}}},\"ref\":\"join\"}";
    bool joined = false;
    bool rejected = false;
    uint32_t openedAt = 0;
    uint32_t heartbeatAt = 0;
    bool awaitingHeartbeat = false;
    WebSocketsClient socket;
    socket.onEvent([&](WStype_t event, uint8_t *payload, size_t length) {
      if (event == WStype_CONNECTED) {
        openedAt = millis();
        if (!socket.sendTXT(join.c_str())) rejected = true;
      } else if (event == WStype_DISCONNECTED) {
        joined = false;
        awaitingHeartbeat = false;
      } else if (event == WStype_TEXT) {
        auto *message = cJSON_ParseWithLength(reinterpret_cast<const char *>(payload), length);
        auto *body = cJSON_GetObjectItemCaseSensitive(message, "payload");
        const char *kind = stringField(message, "event");
        const char *ref = stringField(message, "ref");
        if (strcmp(kind, "phx_reply") == 0 && strcmp(ref, "heartbeat") == 0) awaitingHeartbeat = false;
        if (strcmp(stringField(message, "topic"), channel.c_str()) == 0) {
          if (strcmp(kind, "phx_reply") == 0 && strcmp(ref, "join") == 0) {
            joined = strcmp(stringField(body, "status"), "ok") == 0;
            rejected = !joined;
            if (joined) {
              heartbeatAt = millis();
              Serial.println("WEB: live commands connected");
              xTaskNotifyGive(syncTask);
            } else {
              Serial.println("WEB: live command channel unavailable; checking commands normally.");
            }
          } else if (strcmp(kind, "phx_error") == 0 || strcmp(kind, "phx_close") == 0) {
            rejected = true;
          } else if (joined && strcmp(kind, "broadcast") == 0 && strcmp(stringField(body, "event"), "changed") == 0) {
            xTaskNotifyGive(syncTask);
          }
        }
        cJSON_Delete(message);
      }
    });
    const size_t bundleSize = reinterpret_cast<uintptr_t>(rootBundleEnd) - reinterpret_cast<uintptr_t>(rootBundleStart);
    socket.beginSslWithBundle(host.c_str(), 443, path.c_str(), rootBundleStart, bundleSize, "");
    socket.setReconnectInterval(4000);
    while (WiFi.status() == WL_CONNECTED && !rejected) {
      socket.loop();
      const auto now = millis();
      if (socket.isConnected() && !joined && now - openedAt >= 7500) rejected = true;
      if (joined && now - heartbeatAt >= 20000) {
        const char *heartbeat = "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"heartbeat\"}";
        rejected = awaitingHeartbeat || !socket.sendTXT(heartbeat);
        awaitingHeartbeat = true;
        heartbeatAt = now;
      }
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    socket.disconnect();
    vTaskDelay(pdMS_TO_TICKS(4000));
  }
}

static void networkLoop(void *) {
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.println("WEB: Wi-Fi joined.");
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("WEB: Wi-Fi disconnected (reason %u).\n", info.wifi_sta_disconnected.reason);
    }
  });
  Serial.println("WEB: joining Wi-Fi.");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ISHOOT_WIFI_NAME, ISHOOT_WIFI_PASSWORD);

  const String endpoint = String(ISHOOT_APP_URL) + "/api/game?action=device";
  const String authorization = String("Bearer ") + ISHOOT_DEVICE_TOKEN;
  bool clockSyncStarted = false;
  bool announced = false;
  uint32_t lastClockReport = millis() - 15000;
  uint32_t lastError = millis() - 30000;
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  http.setReuse(true);
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(800));
      continue;
    }
    if (!clockSyncStarted) {
      configTime(0, 0, "time.google.com", "pool.ntp.org");
      clockSyncStarted = true;
    }
    if (time(nullptr) < 1700000000) {
      if (millis() - lastClockReport >= 15000) {
        lastClockReport = millis();
        Serial.println("WEB: waiting for internet clock before HTTPS.");
      }
      vTaskDelay(pdMS_TO_TICKS(800));
      continue;
    }

    Snapshot data;
    xSemaphoreTake(snapshotLock, portMAX_DELAY);
    data = snapshot;
    xSemaphoreGive(snapshotLock);
    Round previous;
    const bool backlog = xQueuePeek(archived, &previous, 0) == pdTRUE;
    const String body = encode(data, backlog ? &previous : nullptr);

    int status = -1;
    bool commandQueued = false;
    if (http.begin(client, endpoint)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Authorization", authorization);
      status = http.POST(body);
      if (status == HTTP_CODE_OK) {
        if (!announced) {
          Serial.println("WEB: board connected");
          announced = true;
        }
        if (backlog) xQueueReceive(archived, &previous, 0);
        commandQueued = acceptCommand(http.getString());
      }
      http.end();
      if (status != HTTP_CODE_OK) client.stop();
    }
    if (status != HTTP_CODE_OK && millis() - lastError >= 30000) {
      lastError = millis();
      Serial.printf("WEB: sync unavailable (%d). Physical controls are ready.\n", status);
    }
    const bool playing = strcmp(data.phase, "playing") == 0;
    const uint32_t interval = status != HTTP_CODE_OK ? 3800
      : commandQueued ? 220 : backlog || playing ? 850 : 4000;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval));
  }
}

static void begin(const GameState &game) {
  archived = xQueueCreate(8, sizeof(Round));
  commands = xQueueCreate(1, sizeof(Command));
  snapshotLock = xSemaphoreCreateMutex();
  if (!archived || !commands || !snapshotLock) {
    Serial.println("WEB: not enough memory. Physical controls are ready.");
    return;
  }
  publish(game, true);
  if (xTaskCreate(networkLoop, "ishoot-web", 16384, nullptr, 1, &syncTask) != pdPASS) {
    Serial.println("WEB: cannot start the connection. Physical controls are ready.");
  } else if (xTaskCreate(commandLoop, "ishoot-live", 12288, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("WEB: live commands unavailable; checking commands normally.");
  }
}
}  // namespace online
#endif
