#pragma once
#include "whas/game/character.h"
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include <array>
#include <cstdint>

class Simulation;
class UI;
struct UIState;

// Game mode: a best-of-3 duel of simultaneous turns against the simulation.
// The rules live in Match (shared with replays and lockstep peers); this
// class adds input, pacing and drawing. The local player can be either slot.
// Offline the opponent stands still; online its plan comes from the server.
class Game {
public:
  bool IsActive() const { return m_active; }
  void SetActive(bool active, Simulation &sim, UI &ui);

  // Start a fresh match (new arena from the seed, round 1)
  void StartMatch(Simulation &sim, uint64_t seed, int localSlot = 0);

  // Handle input and advance the world when a turn is executing. Reads the
  // action bar's requests from state and publishes the clock to it.
  void Update(Simulation &sim, UI &ui, UIState &state);
  void Draw(const Simulation &sim, const UI &ui) const;

  const Match::State &GetMatch() const { return m_match; }

  // For the action bar's time-stop button
  enum class ClockState { Waiting, Running, Stopped, Executing, Over };
  ClockState GetClockState() const;
  float TurnProgress() const; // 0..1 of the turn clock used or executed
  // Same as pressing Space
  void ToggleTime(Simulation &sim);
  // Channel time left this turn, so the hotbar can grey out long casts
  int TicksFree() const;

private:
  struct Slot {
    TurnPlan plan;
    PlanPreview preview;
    Color color;
  };

  enum class RoundState { Playing, RoundOver, MatchOver };

  int Local() const { return m_local; }
  int Opponent() const { return 1 - m_local; }
  Character &LocalCharacter() { return m_match.characters[m_local]; }

  void Update(Simulation &sim, UI &ui);
  void BeginRound(Simulation &sim, int round);
  void ResetOpponent(Simulation &sim);
  void BeginPlanning(Simulation &sim);
  void EnterWaiting();
  void Commit();
  void UpdateWaiting(Simulation &sim);
  void UpdatePlanning(Simulation &sim, UI &ui);
  void UpdateExecuting(Simulation &sim);
  void FinishTurn(Simulation &sim);
  void Notify(const char *text, float seconds);

  void DrawCharacter(const Character &c, Color color, bool drawHp) const;
  void DrawSlotPlan(int slot, const PlanPreview &preview,
                    const Simulation &sim, const UI &ui) const;
  void DrawPendingCasts(const Simulation &sim, const UI &ui) const;
  void DrawHud() const;
  void DrawBanner() const;

  bool m_active = false;
  bool m_arenaReady = false;
  int m_local = 0;
  Match::State m_match;
  std::array<Slot, Match::PLAYERS> m_slots{};
  std::array<int, Match::PLAYERS> m_roundsWon{};
  std::array<Vector2, Match::PLAYERS> m_spawns{};
  TurnController m_turn;
  RoundState m_state = RoundState::Playing;
  // Between turns: the world is frozen until Space stops time to plan
  bool m_waiting = true;
  // Planning clock paused: nothing moves, casts queue at the current spot
  bool m_paused = true;
  int m_turnNumber = 1;
  const char *m_notice = nullptr;
  float m_noticeTime = 0.0f;
  const char *m_banner = nullptr;
  const char *m_bannerSub = nullptr;
  float m_bannerTime = 0.0f;
};
