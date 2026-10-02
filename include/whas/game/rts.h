#pragma once
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Simulation;

// Real-time duels: no planning, both players act at once. A spell can't be
// cast again until as long as its cast time has passed (its cooldown), and
// nobody is held still while casting. Online, inputs travel in short
// batches, played a couple of batches after they were pressed so both
// machines have them in time (lockstep, docs/protocol.md).
namespace Rts {

constexpr int BATCH_TICKS = 6;   // 0.1 s of input per message
constexpr int INPUT_DELAY = 2;   // batches between pressing and doing
constexpr int HASH_EVERY = 10;   // batches between state reports
constexpr int ROUND_SECONDS = 180;
constexpr int ROUND_TICKS = ROUND_SECONDS * TurnController::TICKS_PER_SECOND;
constexpr int ROUND_BATCHES = ROUND_TICKS / BATCH_TICKS;

// One tick of a real-time round. plans hold each player's input for the
// current stretch (a batch online, a single tick offline) and step is the
// tick within it; tick counts from the start of the round. Burns cool off
// every turn's length, as they do between turns.
void ExecuteTick(Simulation &sim, Match::State &state,
                 const std::array<const TurnPlan *, Match::PLAYERS> &plans,
                 int step, int tick);

// When each spell can be cast again, by key (the server's spell id online,
// the hotbar slot offline)
class Cooldowns {
public:
  void Reset() { m_ready.clear(); }
  bool Ready(int64_t key, int tick) const;
  void Use(int64_t key, int tick, int castTicks);
  // 0 when ready, 1 just after casting
  float Remaining(int64_t key, int tick) const;

private:
  struct Entry {
    int readyAt = 0;
    int length = 1;
  };
  std::unordered_map<int64_t, Entry> m_ready;
};

// The local player's side: casts clicked between ticks go out with the next
// tick's input. One wind underfoot cast per tick, as in a pause.
class Controller {
public:
  enum class CastResult { Queued, CoolingDown, SecondFlight };

  void BeginRound();
  CastResult QueueCast(PlannedCast cast, int64_t key, int tick);
  // The next tick's input with the casts queued since the last one
  PlanStep TakeStep(CharacterInput input, PlanCursor cursor);
  const Cooldowns &GetCooldowns() const { return m_cooldowns; }

private:
  Cooldowns m_cooldowns;
  std::vector<PlannedCast> m_pending;
};

// Key the cooldown of a cast by: the server id, or the slot offline
inline int64_t CooldownKey(const PlannedCast &cast, int slot) {
  return cast.spellId != 0 ? cast.spellId : -(slot + 1);
}

} // namespace Rts
