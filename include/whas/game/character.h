#pragma once
#include <raylib.h>
#include <utility>

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

  // Burning: each stretch of contact with fire adds a stack, each stack deals
  // damage every tick. Water clears them; they cool off between turns.
  static constexpr int MAX_BURN_STACKS = 5;
  static constexpr int TICKS_PER_STACK = 12; // contact needed for one stack
  static constexpr float BURN_DPS = 2.5f;    // per stack
  int burnStacks = 0;
  int burnExposure = 0; // ticks in fire towards the next stack

  // Seconds of blindness from light bursts nearby. Deterministic, but only
  // the game's screen reads it (it doesn't change what happens), so it's
  // left out of the match hash; the game takes it with TakeFlash.
  float flash = 0.0f;

  // Which way the sprite faces: the last way the body moved sideways. Only
  // drawing reads it, so like flash it stays out of the match hash (facing,
  // which aims casts, only follows walking input).
  int look = 1;

  // Where the player's cursor is (cells), from the plan: sights set spells
  // follow it
  Vector2 cursor{0.0f, 0.0f};
  bool hasCursor = false;
  float TakeFlash() { return std::exchange(flash, 0.0f); }

  // Riding wind underfoot, from take-off until landing. Only the trail and
  // the sound read it, so like flash it stays out of the match hash.
  bool flying = false;

  // Advance one step against the current grid. Deterministic for a given grid,
  // so planning and execution produce the same motion on unchanged terrain.
  void Step(const Simulation &sim, CharacterInput input, float dt);

  // Add velocity from a flight spell, gust or knockback
  void Launch(Vector2 velocity);
  // A flight spell: launch and fly until landing
  void LaunchFlight(Vector2 velocity) {
    Launch(velocity);
    flying = true;
  }

  // One tick of burning against the current grid: gain or clear stacks and
  // take the damage. Deterministic, part of lockstep state.
  void UpdateBurn(const Simulation &sim, float dt);
  // Called at the end of every turn
  void CoolBurn() { burnStacks = burnStacks > 2 ? burnStacks - 2 : 0; }
  bool Burning() const { return burnStacks > 0; }

  Rectangle Bounds() const { return {pos.x, pos.y, WIDTH, HEIGHT}; }
  Vector2 Center() const {
    return {pos.x + WIDTH * 0.5f, pos.y + HEIGHT * 0.5f};
  }
  bool Alive() const { return hp > 0.0f; }
};
