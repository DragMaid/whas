#include "whas/game/turn_controller.h"
#include "whas/audio/audio_manager.h"
#include "whas/engine/simulation.h"
#include <algorithm>
#include <climits>
#include <cmath>

PlannedCast PlannedCast::Local(const Spell &spell, Vector2 aim) {
  SpellQuant::Aim q = SpellQuant::QuantizeAim(aim);
  return {spell, SpellQuant::Canonical(spell), 0, q,
          SpellQuant::DequantizeAim(q)};
}

void PlannedCast::PlaceAt(Vector2 at, Vector2 caster) {
  auto q = [](float v) {
    return static_cast<int16_t>(
        std::clamp<long>(std::lround(v * PLACE_SCALE), INT16_MIN, INT16_MAX));
  };
  placed = true;
  px = q(at.x - caster.x);
  py = q(at.y - caster.y);
}

Vector2 PlannedCast::Origin(Vector2 caster) const {
  if (!placed || stats.HasFlight())
    return caster;
  return {caster.x + static_cast<float>(px) / PLACE_SCALE,
          caster.y + static_cast<float>(py) / PLACE_SCALE};
}

void PlanPreview::Reset(const Character &start) {
  end = start;
  path.clear();
  casts.clear();
}

// Simulate the next unpreviewed step of the plan
void PlanPreview::Append(const Simulation &sim, const TurnPlan &plan,
                         float dt) {
  const PlanStep &step = plan.steps[path.size()];
  for (const PlannedCast &cast : step.casts) {
    const SpellStats &stats = cast.stats;
    if (!end.CanCast(stats.HasFlight()))
      continue;
    if (stats.HasFlight())
      end.wet = 0.0f;
    Vector2 dir = SpellSystem::ResolveDirection(stats, cast.aim);
    casts.push_back({cast.spell, cast.Origin(end.Center()), dir, stats});
    if (stats.HasFlight())
      end.Launch(SpellSystem::FlightVelocity(stats, cast.aim));
  }
  end.Step(sim, step.input, dt);
  path.push_back(end.Center());
}

PlanPreview PreviewPlan(const Simulation &sim, const Character &start,
                        const TurnPlan &plan, float dt) {
  PlanPreview preview;
  preview.Reset(start);
  while (preview.path.size() < plan.steps.size())
    preview.Append(sim, plan, dt);
  return preview;
}

void TurnController::BeginPlanning(const Character &localPlayer) {
  m_phase = Phase::Planning;
  m_start = localPlayer;
  m_plan.steps.clear();
  m_preview.Reset(localPlayer);
  m_segmentStarts.clear();
  m_segmentBreak = true;
  m_pending.clear();
  m_channelTicks = 0;
  m_execTick = 0;
}

int TurnController::CastTicks(const SpellStats &stats) {
  if (stats.kind == SpellKind::Compound) {
    // The slowest part in full, half of each other part, and a little to
    // bind the layers: slower than any one part, quicker than casting them
    // one by one. Half of the rest is rounded up.
    int longest = 0, sum = 0;
    for (const SpellStats &part : stats.parts) {
      int t = CastTicks(part);
      longest = std::max(longest, t);
      sum += t;
    }
    int parts = static_cast<int>(stats.parts.size());
    int rest = (sum - longest + 1) / 2;
    return std::max(1, longest + rest + 8 + 2 * std::max(0, parts - 1));
  }
  // 0.3s + 0.005s per particle = 18 ticks + 0.3 ticks per particle, rounded
  // half up: (180 + 3 * particles + 5) / 10
  int ticks = (18 * 10 + 3 * stats.particleCount + 5) / 10;
  return std::max(1, ticks);
}

TurnController::CastResult TurnController::QueueCast(PlannedCast cast) {
  if (m_phase != Phase::Planning)
    return CastResult::NoTime;

  if (cast.stats.HasFlight() &&
      std::any_of(m_pending.begin(), m_pending.end(),
                  [](const PlannedCast &c) { return c.stats.HasFlight(); }))
    return CastResult::SecondFlight;

  int ticks = CastTicks(cast.stats);
  if (ticks > TicksFree())
    return CastResult::NoTime;

  m_pending.push_back(std::move(cast));
  m_channelTicks += ticks;
  return CastResult::Queued;
}

PlanCursor PlanCursor::FromCells(Vector2 cells) {
  auto q = [](float v) {
    return static_cast<int16_t>(std::clamp<long>(
        std::lround(v * SCALE), INT16_MIN, INT16_MAX));
  };
  return {true, q(cells.x), q(cells.y)};
}

void TurnController::FlowTick(const Simulation &sim, CharacterInput input,
                              PlanCursor cursor) {
  if (m_phase != Phase::Planning || Finished())
    return;

  PlanStep step;
  step.cursor = cursor;
  step.casts = std::move(m_pending);
  m_pending.clear();
  if (m_channelTicks > 0) {
    input = {}; // standing still while the cast is channelled
    m_channelTicks--;
  }
  step.input = input;

  // A new undo segment at every pause, every cast and every change of keys
  bool newSegment = m_segmentBreak || m_plan.steps.empty() ||
                    !step.casts.empty() ||
                    !(m_plan.steps.back().input == input);
  if (newSegment)
    m_segmentStarts.push_back(m_plan.steps.size());
  m_segmentBreak = false;

  m_plan.steps.push_back(std::move(step));
  m_preview.Append(sim, m_plan, TICK_DT);
}

void TurnController::Undo(const Simulation &sim) {
  if (m_phase != Phase::Planning)
    return;

  if (!m_pending.empty()) {
    m_channelTicks -= CastTicks(m_pending.back().stats);
    m_pending.pop_back();
    return;
  }
  if (m_segmentStarts.empty())
    return;

  size_t cut = m_segmentStarts.back();
  m_segmentStarts.pop_back();
  // Casts that fired at the start of the undone run go back to being queued,
  // so undo steps back to the paused moment rather than losing them
  m_pending = std::move(m_plan.steps[cut].casts);
  m_plan.steps.resize(cut);
  m_segmentBreak = true;

  // Casts before the cut may still be channelling past it: replay the
  // bookkeeping FlowTick does (casts owe time, each tick pays one)
  int owed = 0;
  for (const PlanStep &step : m_plan.steps) {
    for (const PlannedCast &cast : step.casts)
      owed += CastTicks(cast.stats);
    owed = std::max(0, owed - 1);
  }
  for (const PlannedCast &cast : m_pending)
    owed += CastTicks(cast.stats);
  m_channelTicks = owed;
  m_preview = PreviewPlan(sim, m_start, m_plan, TICK_DT);
}

void TurnController::Flush(const Simulation &sim) {
  while (!Finished() && (!m_pending.empty() || m_channelTicks > 0))
    FlowTick(sim, {});
}

void TurnController::BeginExecution() {
  m_phase = Phase::Executing;
  m_execTick = 0;
}

bool TurnController::Advance() {
  if (m_phase != Phase::Executing || m_execTick >= TURN_TICKS)
    return false;
  return ++m_execTick < TURN_TICKS;
}

void TurnController::ApplyPlanTick(const TurnPlan &plan, int tick,
                                   Simulation &sim, Character &character) {
  CharacterInput input{};
  if (tick >= 0 && tick < static_cast<int>(plan.steps.size())) {
    const PlanStep &step = plan.steps[tick];
    input = step.input;
    // Past the end of the plan the last cursor stays where it was
    if (step.cursor.set) {
      character.cursor = step.cursor.Cells();
      character.hasCursor = true;
    }
    for (const PlannedCast &cast : step.casts) {
      if (!character.Alive())
        break;
      // Wet paper won't take a spell; a flight dries it off
      if (!character.CanCast(cast.stats.HasFlight()))
        continue;
      if (cast.stats.HasFlight())
        character.wet = 0.0f;
      // Cast from where the caster stands, then any flight carries them off
      sim.CastSpell(cast.stats, cast.Origin(character.Center()), cast.aim,
                    character.id);
      if (cast.stats.HasFlight()) {
        character.LaunchFlight(SpellSystem::FlightVelocity(cast.stats, cast.aim));
        AudioManager::EmitFlightLaunch(character.Center().x);
      }
    }
  }
  // The fallen don't walk, but they still fall
  character.Step(sim, character.Alive() ? input : CharacterInput{}, TICK_DT);
}
