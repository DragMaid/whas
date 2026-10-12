#pragma once
#include <optional>
#include <raylib.h>

class Simulation;

// Placed casts (right click): the spell is drawn on the ground or a wall near the
// caster and fires from there instead of from the body
namespace Placement {

constexpr float REACH = 48.0f; // cells from the caster's centre
constexpr int SNAP = 8;        // cells around the cursor searched for a surface

struct Spot {
  Vector2 pos;    // cells, an open cell touching the surface
  Vector2 normal; // unit, pointing out of the surface
};

// The open cell touching solid terrain nearest the cursor, within reach
std::optional<Spot> FindSurface(const Simulation &sim, Vector2 cursor,
                                Vector2 caster);

} // namespace Placement

// Mouse handling for casting, shared by every mode that casts. Left click
// fires from the body at the cursor; right click draws the spell on the
// ground or wall nearest the cursor and fires it from there, the same way
// (or straight out of the surface when that way leads into it).
class CastTargeting {
public:
  struct Target {
    Vector2 aim;               // unit
    std::optional<Vector2> at; // placed: where the spell is drawn (cells)
    Vector2 normal{0.0f, -1.0f}; // placed: out of the surface
  };

  // Mouse in cells; worldInput is false while a panel has the mouse. A
  // target when a cast should fire this frame; `blocked` explains a miss.
  static std::optional<Target> Update(const Simulation &sim, Vector2 caster,
                                      Vector2 mouse, bool worldInput,
                                      int facing, const char **blocked);
  // The spot a right click would draw on, if there's one in reach
  static void DrawWorld(const Simulation &sim, Vector2 caster, Vector2 mouse);
};
