#pragma once
#include <raylib.h>

class Simulation;

struct CharacterInput {
  bool left = false;
  bool right = false;
  bool jump = false;

  bool Any() const { return left || right || jump; }
  bool operator==(const CharacterInput &) const = default;
};

// A walking body that lives on top of the grid (never written into it).
// Positions and sizes are in cells; pos is the top-left corner.
struct Character {
  static constexpr float WIDTH = 3.0f;
  static constexpr float HEIGHT = 6.0f;
  static constexpr float MASS = 3.0f;     // for gust pushes
  static constexpr float GRAVITY = 90.0f; // cells/s^2 at world gravity 1

  int id = 0;
  Vector2 pos{0.0f, 0.0f};
  Vector2 vel{0.0f, 0.0f};
  // Horizontal velocity from launches and pushes, on top of walking. Bleeds
  // off quickly on the ground and slowly in the air.
  float pushX = 0.0f;
  bool grounded = false;
  int facing = 1;
  float hp = 100.0f;
  float maxHp = 100.0f;

  // Advance one step against the current grid. Deterministic for a given grid,
  // so planning and execution produce the same motion on unchanged terrain.
  void Step(const Simulation &sim, CharacterInput input, float dt);

  // Add velocity from a flight spell, gust or knockback
  void Launch(Vector2 velocity);

  Rectangle Bounds() const { return {pos.x, pos.y, WIDTH, HEIGHT}; }
  Vector2 Center() const {
    return {pos.x + WIDTH * 0.5f, pos.y + HEIGHT * 0.5f};
  }
  bool Alive() const { return hp > 0.0f; }
};
