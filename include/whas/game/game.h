#pragma once
#include "whas/game/character.h"
#include "whas/game/turn_controller.h"
#include <array>

class Simulation;
class UI;

// Game mode: two players taking simultaneous turns against the simulation.
// Slot 0 is planned locally. Slot 1 is the opponent, whose plan will come from
// the network; until then it stays empty and the opponent stands still.
// Sandbox mode (the painting tools) runs when this is disabled.
class Game {
public:
  bool IsActive() const { return m_active; }
  void SetActive(bool active, Simulation &sim, UI &ui);

  // Handle input and advance the world when a turn is executing
  void Update(Simulation &sim, UI &ui);
  void Draw(const Simulation &sim, const UI &ui) const;

private:
  static constexpr int LOCAL = 0;
  static constexpr int OPPONENT = 1;

  struct Slot {
    Character character;
    TurnPlan plan;
    PlanPreview preview;
    Vector2 spawn;
    float maxHp;
    Color color;
  };

  enum class RoundState { Playing, Defeated };

  void ResetArena(Simulation &sim);
  void SetupTerrain(Simulation &sim);
  void Respawn(Simulation &sim, int slot);
  void BeginPlanning(Simulation &sim);
  void EnterWaiting();
  void Commit();
  void UpdateWaiting(Simulation &sim);
  void UpdatePlanning(Simulation &sim, UI &ui);
  void UpdateExecuting(Simulation &sim);
  void FinishTurn(Simulation &sim);
  void ApplyHits(Simulation &sim);
  void ApplyGusts(const Simulation &sim);
  void Notify(const char *text, float seconds);

  void DrawCharacter(const Character &c, Color color, bool drawHp) const;
  void DrawSlotPlan(const Slot &slot, const PlanPreview &preview,
                    const Simulation &sim, const UI &ui) const;
  void DrawPendingCasts(const Simulation &sim, const UI &ui) const;
  void DrawHud() const;
  void DrawBanner() const;

  bool m_active = false;
  bool m_arenaReady = false;
  std::array<Slot, 2> m_slots{};
  TurnController m_turn;
  RoundState m_state = RoundState::Playing;
  // Between turns: the world is frozen until Space stops time to plan
  bool m_waiting = true;
  // Planning clock paused: nothing moves, casts queue at the current spot
  bool m_paused = true;
  int m_turnNumber = 1;
  int m_round = 1;
  const char *m_notice = nullptr;
  float m_noticeTime = 0.0f;
  const char *m_banner = nullptr;
  float m_bannerTime = 0.0f;
};
