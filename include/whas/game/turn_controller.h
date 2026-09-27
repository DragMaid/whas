#pragma once
#include "whas/game/character.h"
#include "whas/spell/spell_system.h"
#include "whas/spell/spell_quant.h"
#include "whas/spell/spell_types.h"
#include <vector>

class Simulation;

struct PlannedCast {
  Spell spell;       // for drawing only; never evaluated during execution
  SpellStats stats;  // authoritative: from the server, or SpellQuant::Canonical
  int64_t spellId = 0; // server spell definition (0 offline)
  SpellQuant::Aim aimQ; // aim as sent over the wire
  Vector2 aim;       // DequantizeAim(aimQ): what the simulation uses

  // Offline: stats computed locally with the server's rounding
  static PlannedCast Local(const Spell &spell, Vector2 aim);
};

// One tick of input. Its casts fire at the start of the tick, before moving;
// several can share a tick when they were queued during the same pause.
struct PlanStep {
  CharacterInput input;
  std::vector<PlannedCast> casts;
};

// Everything one player does in a turn. Plain data, so it can be exchanged
// between clients once online play exists.
struct TurnPlan {
  std::vector<PlanStep> steps;
};

// Where a plan takes a character on the current (frozen) terrain
struct PlanPreview {
  struct CastMark {
    Spell spell;
    Vector2 origin;     // character center when it fires
    Vector2 direction;
    SpellStats stats;
  };

  Character end;
  std::vector<Vector2> path; // character center after each step
  std::vector<CastMark> casts;

  void Reset(const Character &start);
  void Append(const Simulation &sim, const TurnPlan &plan, float dt);
};

PlanPreview PreviewPlan(const Simulation &sim, const Character &start,
                        const TurnPlan &plan, float dt);

// Transistor-style turns. Planning runs on a 3 second clock against a frozen
// world: the local player can pause it to queue casts from exactly where they
// are, and unpause to let the clock run (walking, falling, channelling). When
// the clock runs out every player's plan executes over one turn of world time.
// This class edits the local player's plan and keeps the turn clock.
class TurnController {
public:
  static constexpr int TICKS_PER_SECOND = 60;
  static constexpr float TICK_DT = 1.0f / TICKS_PER_SECOND;
  static constexpr float TURN_SECONDS = 3.0f;
  static constexpr int TURN_TICKS =
      static_cast<int>(TURN_SECONDS * TICKS_PER_SECOND);

  enum class Phase { Planning, Executing };

  enum class CastResult { Queued, NoTime, SecondFlight };

  Phase GetPhase() const { return m_phase; }

  void BeginPlanning(const Character &localPlayer);

  // Queue a cast to fire from where the ghost is now, on the next tick of the
  // clock. Nothing moves until the clock runs; the cast's channel time is then
  // spent standing still. Only one movement (wind) spell per pause.
  CastResult QueueCast(PlannedCast cast);

  // Run the clock one tick: pending casts fire, then the ghost moves (input is
  // ignored while channelling).
  void FlowTick(const Simulation &sim, CharacterInput input);

  // Call when the clock is paused, so undo can step back to this point
  void MarkPause() { m_segmentBreak = true; }

  // Drop the latest pending cast, or else the latest run of the clock (its
  // opening casts become pending again)
  void Undo(const Simulation &sim);

  // Run the clock just long enough to fire pending casts and finish their
  // channelling (used when executing early)
  void Flush(const Simulation &sim);

  void BeginExecution();

  // Move the turn clock forward one tick; false once the turn is over.
  // Callers apply every player's plan for ExecutedTicks() before advancing.
  bool Advance();

  // Apply one tick of any plan to its character (casts, then move). Ticks
  // past the end of the plan are spent standing still.
  static void ApplyPlanTick(const TurnPlan &plan, int tick, Simulation &sim,
                            Character &character);

  // Clock time a cast spends channelling. Integer math on quantized stats, so
  // the server (plan validation) computes exactly the same number.
  static int CastTicks(const SpellStats &stats);

  int TicksUsed() const { return static_cast<int>(m_plan.steps.size()); }
  // Clock time not yet spoken for by movement or channelling
  int TicksFree() const { return TURN_TICKS - TicksUsed() - m_channelTicks; }
  bool Finished() const { return TicksUsed() >= TURN_TICKS; }
  bool Channelling() const { return m_channelTicks > 0; }
  int ExecutedTicks() const { return m_execTick; }

  const TurnPlan &LocalPlan() const { return m_plan; }
  const PlanPreview &LocalPreview() const { return m_preview; }
  const std::vector<PlannedCast> &PendingCasts() const { return m_pending; }

private:
  Phase m_phase = Phase::Planning;
  Character m_start;
  TurnPlan m_plan;
  PlanPreview m_preview;
  std::vector<size_t> m_segmentStarts;
  bool m_segmentBreak = true;

  std::vector<PlannedCast> m_pending; // queued this pause, fire next tick
  int m_channelTicks = 0;             // cast time still to be spent
  int m_execTick = 0;
};
