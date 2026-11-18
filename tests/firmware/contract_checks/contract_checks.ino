#include "../../../creation/code/ishootmo_code/ishootmo_code.ino"

constexpr bool checkPrecision() {
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    GameState s;
    s.start(PRECISION, 100, seed);
    int counts[3] = {0, 0, 0};
    for (int i = 0; i < 6; ++i) {
      ++counts[s.activeZone];
      if (s.recordThrow(s.activeZone, 100 + i * 650) != CORRECT_HIT) return false;
    }
    if (counts[0] != 2 || counts[1] != 2 || counts[2] != 2) return false;
    if (s.score != 120 || s.hits != 6 || s.throwsUsed != 6) return false;
    if (s.running || !s.finished || s.activeZone != -1) return false;
  }
  return true;
}
constexpr bool checkPrecisionMisses() {
  GameState s;
  s.start(PRECISION, 0, 9);
  if (s.recordThrow((s.activeZone + 1) % 3, 0) != WRONG_HOLE) return false;
  for (int i = 1; i < 6; ++i) s.recordThrow(-1, i * 650);
  return s.finished && s.score == 0 && s.wrong == 1 && s.misses == 5;
}
constexpr bool checkChaseBalanceAndFairness() {
  GameState a, b;
  a.start(TARGET_CHASE, 10, CHASE_SEQUENCE_SEED);
  b.start(TARGET_CHASE, 1000, CHASE_SEQUENCE_SEED);
  int previous = -1;
  for (int group = 0; group < 20; ++group) {
    int counts[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
      if (a.activeZone != b.activeZone) return false;
      if (a.activeZone == previous) return false;
      previous = a.activeZone;
      ++counts[a.activeZone];
      int n = group * 3 + i;
      if (a.recordThrow(a.activeZone, 10 + n * 650) != CORRECT_HIT) return false;
      if (b.recordThrow(b.activeZone, 1000 + n * 650) != CORRECT_HIT) return false;
    }
    if (counts[0] != 1 || counts[1] != 1 || counts[2] != 1) return false;
  }
  return a.running && a.score == 1200 && a.hits == 60;
}
constexpr bool checkChaseMisses() {
  GameState s;
  s.start(TARGET_CHASE, 0, CHASE_SEQUENCE_SEED);
  int target = s.activeZone, index = s.nextIndex;
  s.recordThrow((target + 1) % 3, 0);
  s.recordThrow(-1, 650);
  return s.activeZone == target && s.nextIndex == index &&
         s.score == 0 && s.wrong == 1 && s.misses == 1 && s.throwsUsed == 2;
}
constexpr bool checkLockout() {
  GameState s;
  s.start(PRECISION, 100, 3);
  if (s.recordThrow(s.activeZone, 100) != CORRECT_HIT) return false;
  if (s.recordThrow(s.activeZone, 749) != IGNORED) return false;
  if (s.recordThrow(-1, 101) != IGNORED) return false;
  if (s.recordThrow(s.activeZone, 750) != CORRECT_HIT) return false;
  return s.hits == 2 && s.throwsUsed == 2;
}
constexpr bool checkDeadline() {
  GameState s;
  s.start(TARGET_CHASE, 1234, CHASE_SEQUENCE_SEED);
  if (s.expire(1234) || s.expire(1235) || s.expire(61233)) return false;
  if (s.recordThrow(s.activeZone, 61234) != IGNORED) return false;
  return s.finished && !s.running && s.score == 0;
}
constexpr bool checkRollover() {
  GameState s;
  constexpr uint32_t start = 0xffffff00U;
  s.start(TARGET_CHASE, start, CHASE_SEQUENCE_SEED);
  if (s.recordThrow(s.activeZone, start) != CORRECT_HIT) return false;
  if (s.recordThrow(s.activeZone, uint32_t(start + 650U)) != CORRECT_HIT) return false;
  if (s.expire(uint32_t(start + 59999U))) return false;
  return s.expire(uint32_t(start + 60000U)) && s.finished && s.hits == 2;
}
constexpr bool checkResetAndInvalidInputs() {
  GameState s;
  if (s.recordThrow(0, 0) != IGNORED) return false;
  s.start(TARGET_CHASE, 0, 0);
  if (s.recordThrow(3, 0) != IGNORED || s.recordThrow(-2, 0) != IGNORED) return false;
  s.recordThrow(s.activeZone, 0);
  s.clear();
  return s.mode == TARGET_CHASE && !s.running && !s.finished &&
         s.activeZone == -1 && s.score == 0 && s.throwsUsed == 0;
}
static_assert(checkPrecision(), "Precision balance, max score and six-throw finish");
static_assert(checkPrecisionMisses(), "Wrong holes and misses consume throws");
static_assert(checkChaseBalanceAndFairness(), "Chase balanced and repeatable");
static_assert(checkChaseMisses(), "Only correct Chase hits advance targets");
static_assert(checkLockout(), "First throw immediate, duplicates blocked");
static_assert(checkDeadline(), "Exactly 60 seconds, no late scores");
static_assert(checkRollover(), "Timers work across millis rollover");
static_assert(checkResetAndInvalidInputs(), "Reset clears and retains mode");

constexpr bool checkCountdownSounds() {
  CountdownSound cue;
  if (cue.takeCue(49999) != -1) return false;
  for (int second = 10; second >= 0; --second) {
    uint32_t elapsed = 60000U - uint32_t(second) * 1000U;
    if (cue.takeCue(elapsed) != second) return false;
    if (cue.takeCue(elapsed + 1) != -1) return false;
  }
  if (cue.takeCue(61000) != -1) return false;
  cue.lastSecond = -1;
  return cue.takeCue(0) == -1 && cue.takeCue(50000) == 10;
}
constexpr bool checkSoundQueue() {
  SoundQueue q;
  if (q.pop() != 0) return false;
  for (int i = 1; i <= 8; ++i) {
    if (!q.push(i * 50)) return false;
  }
  if (q.push(999)) return false;
  if (q.pop() != 50 || q.pop() != 100) return false;
  if (!q.push(450) || !q.push(500)) return false;
  for (int i = 3; i <= 10; ++i) {
    if (q.pop() != i * 50) return false;
  }
  q.push(80);
  q.clear();
  return q.count == 0 && q.pop() == 0 && q.push(120) && q.pop() == 120;
}
static_assert(checkCountdownSounds(), "One cue per second from 10 through zero, with reset");
static_assert(checkSoundQueue(), "Overlapping cues preserve order; capacity, wrap and clear");
