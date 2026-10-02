#pragma once
#include "whas/game/character.h"
#include "whas/game/flight_trail.h"
#include "whas/game/match.h"
#include "whas/game/rts.h"
#include "whas/game/turn_controller.h"
#include <array>
#include <cstdint>
#include <string>
#include <utility>

class Simulation;
class UI;
struct UIState;
class LockstepClient;

// Game mode: a best-of-3 duel of simultaneous turns against the simulation.
// The rules live in Match (shared with replays and lockstep peers); this
// class adds input, pacing and drawing. The local player can be either slot.
// Offline the opponent stands still; online its plan comes from the server.
class Game {
public:
  bool IsActive() const { return m_active; }
  void SetActive(bool active, Simulation &sim, UI &ui);

  // Start a fresh match (round 1 on the options' first map, or a new arena
  // from the seed)
  void StartMatch(Simulation &sim, uint64_t seed, int localSlot = 0,
                  MatchOptions options = {});
  const MatchOptions &Options() const { return m_options; }
  // Real time instead of planned turns
  bool IsRts() const;

  // Online: the client runs the turn flow (the server's clock, both plans,
  // hashes); the game lets the local player plan in between and shows it
  void StartOnline(Simulation &sim, UI &ui, LockstepClient &client);
  bool IsOnline() const { return m_online != nullptr; }
  // Back to the sandbox after an online match
  void LeaveOnline(UI &ui);
  // The online match is over and the player pressed Enter
  bool TakeExitRequest() { return std::exchange(m_exitRequested, false); }

  // Handle input and advance the world when a turn is executing. Reads the
  // action bar's requests from state and publishes the clock to it.
  void Update(Simulation &sim, UI &ui, UIState &state);
  void Draw(const Simulation &sim, const UI &ui) const;

  const Match::State &GetMatch() const { return m_match; }
  // The network client builds rounds into this even before the game has
  // switched to online mode (several messages can arrive in one frame)
  Match::State &NetState() { return m_match; }

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
  void UpdateOnline(Simulation &sim, UI &ui);
  void BeginRound(Simulation &sim, int round);
  PlannedCast MakeCast(const Spell &spell, Vector2 aim) const;
  void ResetOpponent(Simulation &sim);
  void BeginPlanning(Simulation &sim);
  void EnterWaiting();
  void Commit();
  void UpdateWaiting(Simulation &sim);
  void UpdatePlanning(Simulation &sim, UI &ui);
  void UpdateExecuting(Simulation &sim);
  void FinishTurn(Simulation &sim);
  // winner: a slot, Match::PLAYERS for a double KO or a draw
  void EndRound(int winner);
  void UpdateRts(Simulation &sim, UI &ui);
  // Real time: clicks become casts on the next tick
  void QueueRtsCast(UI &ui, int tick);
  CharacterInput RtsInput() const;
  void DrawRtsHud() const;
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
  MatchOptions m_options; // offline; online uses the client's
  std::array<Slot, Match::PLAYERS> m_slots{};
  std::array<int, Match::PLAYERS> m_roundsWon{};
  std::array<Vector2, Match::PLAYERS> m_spawns{};
  TurnController m_turn;
  Rts::Controller m_rts;
  int m_rtsTick = 0; // ticks played this round
  float m_rtsAccumulator = 0.0f;
  FlightTrail m_trail;
  RoundState m_state = RoundState::Playing;
  // Between turns: the world is frozen until Space stops time to plan
  bool m_waiting = true;
  // Planning clock paused: nothing moves, casts queue at the current spot
  bool m_paused = true;
  int m_turnNumber = 1;
  LockstepClient *m_online = nullptr;
  bool m_submitted = false;   // online: this turn's plan is committed
  int m_plannedTurn = -1;     // online: round * 1000 + turn being planned
  int m_seenRoundEnds = 0;
  bool m_exitRequested = false;
  std::string m_noticeText;
  const char *m_notice = nullptr;
  float m_noticeTime = 0.0f;
  const char *m_banner = nullptr;
  const char *m_bannerSub = nullptr;
  float m_bannerTime = 0.0f;
};
