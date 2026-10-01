#pragma once
#include "whas/game/replay.h"
#include "whas/game/flight_trail.h"
#include <string>

class Simulation;
struct UIState;

// Watching a stored match: plays the replay at 60 ticks a second (or
// faster), with pause, step and restart, and shows whether each turn
// reproduced the hashes the players reported
class ReplayView {
public:
  bool Open(const nlohmann::json &replay, Simulation &sim);
  bool Active() const { return m_active; }
  void Close() { m_active = false; }

  void Update(Simulation &sim, UIState &state);
  void Draw() const;
  void DrawControls(Simulation &sim); // ImGui window

  const std::string &Error() const { return m_error; }

private:
  ReplayPlayer m_player;
  FlightTrail m_trail;
  bool m_active = false;
  bool m_playing = true;
  int m_speed = 1;
  std::string m_error;
};
