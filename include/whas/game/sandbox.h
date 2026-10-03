#pragma once
#include "whas/game/character.h"
#include "whas/game/flight_trail.h"
#include "whas/game/placement.h"
#include "whas/game/turn_controller.h"
#include <vector>

class Simulation;
class UI;
struct UIState;

// Free play: paint the world (Draw tool) or test spells from an avatar
// (Cast tool), never both at once. Time runs in real time; the time-stop
// button freezes it so several casts can be queued and fired together,
// spaced by their cast times like in a match. The avatar doesn't walk: drag
// it, or right-click to put it somewhere.
class Sandbox {
public:
  void Update(Simulation &sim, UI &ui, UIState &state);
  void Draw(const Simulation &sim, const UI &ui, const UIState &state) const;

private:
  void EnsureAvatar(Simulation &sim);
  void PlaceAvatar(const Simulation &sim, Vector2 cellPos);
  void ResetAvatar(const Simulation &sim);
  void ToggleTime();
  void HandleDraw(Simulation &sim, UI &ui, UIState &state);
  void HandleCast(Simulation &sim, UI &ui);
  void Fire(Simulation &sim, const PlannedCast &cast);
  void Tick(Simulation &sim, bool isPainting);
  Vector2 AimAtMouse() const;
  int QueuedTicks() const;

  Character m_avatar;
  FlightTrail m_trail;
  CastTargeting m_targeting;
  bool m_hasAvatar = false;
  Vector2 m_home{0, 0}; // where Reset puts the avatar
  bool m_dragging = false;
  Vector2 m_dragOffset{0, 0};

  bool m_stopped = false;
  std::vector<PlannedCast> m_queued; // while time is stopped
  struct Scheduled {
    PlannedCast cast;
    int tick;
  };
  std::vector<Scheduled> m_scheduled; // released queue, fired over time
  int m_tick = 0;
  float m_accumulator = 0.0f;
};
