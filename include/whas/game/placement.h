#pragma once
#include <optional>
#include <raylib.h>

class Simulation;

// Placed casts (Q): the spell is drawn on the ground or a wall near the
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

// Mouse handling for casting, shared by every mode that casts. Normally a
// press fires at the cursor from the body. With placement on (Q), a press
// picks a surface spot, dragging sets the aim (no drag: straight out of the
// surface) and the release fires.
class CastTargeting {
public:
  struct Target {
    Vector2 aim;               // unit
    std::optional<Vector2> at; // placed: where the spell is drawn (cells)
  };

  void Toggle() { m_placing = !m_placing, m_held.reset(); }
  bool Placing() const { return m_placing; }
  void Cancel() { m_held.reset(); }

  // Mouse in cells; worldInput is false while a panel has the mouse. A
  // target when a cast should fire this frame; `blocked` explains a miss.
  std::optional<Target> Update(const Simulation &sim, Vector2 caster,
                               Vector2 mouse, bool worldInput, int facing,
                               const char **blocked);
  // Reach, the surface spot under the cursor and the aim being dragged
  void DrawWorld(const Simulation &sim, Vector2 caster, Vector2 mouse) const;

private:
  bool m_placing = false;
  std::optional<Placement::Spot> m_held;
};
