#include "whas/game/rts.h"
#include <algorithm>

namespace Rts {

void ExecuteTick(Simulation &sim, Match::State &state,
                 const std::array<const TurnPlan *, Match::PLAYERS> &plans,
                 int step, int tick) {
  Match::ExecuteTick(sim, state, plans, step);
  if ((tick + 1) % TurnController::TURN_TICKS == 0)
    Match::EndTurn(state);
}

bool Cooldowns::Ready(int64_t key, int tick) const {
  auto it = m_ready.find(key);
  return it == m_ready.end() || tick >= it->second.readyAt;
}

void Cooldowns::Use(int64_t key, int tick, int castTicks) {
  castTicks = std::max(1, castTicks);
  m_ready[key] = {tick + castTicks, castTicks};
}

float Cooldowns::Remaining(int64_t key, int tick) const {
  auto it = m_ready.find(key);
  if (it == m_ready.end() || tick >= it->second.readyAt)
    return 0.0f;
  return static_cast<float>(it->second.readyAt - tick) / it->second.length;
}

void Controller::BeginRound() {
  m_cooldowns.Reset();
  m_pending.clear();
}

Controller::CastResult Controller::QueueCast(PlannedCast cast, int64_t key,
                                             int tick) {
  if (!m_cooldowns.Ready(key, tick))
    return CastResult::CoolingDown;
  if (cast.stats.HasFlight() &&
      std::any_of(m_pending.begin(), m_pending.end(),
                  [](const PlannedCast &c) { return c.stats.HasFlight(); }))
    return CastResult::SecondFlight;
  m_cooldowns.Use(key, tick, TurnController::CastTicks(cast.stats));
  m_pending.push_back(std::move(cast));
  return CastResult::Queued;
}

PlanStep Controller::TakeStep(CharacterInput input, PlanCursor cursor) {
  PlanStep step;
  step.input = input;
  step.cursor = cursor;
  step.casts = std::move(m_pending);
  m_pending.clear();
  return step;
}

} // namespace Rts
