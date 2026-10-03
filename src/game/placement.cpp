#include "whas/game/placement.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include <cmath>

namespace Placement {

namespace {

bool Solid(const Simulation &sim, int x, int y) {
  if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H)
    return false;
  const Cell &c = sim.GetCell(x, y);
  if (c.element == Element::AIR)
    return false;
  const auto &props = sim.GetConfig().elements[static_cast<size_t>(c.element)];
  return props.solid && !props.passable;
}

float Dist2(Vector2 a, Vector2 b) {
  return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

} // namespace

std::optional<Spot> FindSurface(const Simulation &sim, Vector2 cursor,
                                Vector2 caster) {
  int cx = static_cast<int>(std::floor(cursor.x));
  int cy = static_cast<int>(std::floor(cursor.y));
  std::optional<Spot> best;
  float bestD = 1e30f;
  for (int y = cy - SNAP; y <= cy + SNAP; ++y) {
    for (int x = cx - SNAP; x <= cx + SNAP; ++x) {
      if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H || Solid(sim, x, y))
        continue;
      Vector2 at{x + 0.5f, y + 0.5f};
      if (Dist2(at, caster) > REACH * REACH)
        continue;
      // Out of the surface: away from the solid neighbours
      Vector2 n{0.0f, 0.0f};
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if ((dx || dy) && Solid(sim, x + dx, y + dy)) {
            n.x -= dx;
            n.y -= dy;
          }
      float len = std::hypot(n.x, n.y);
      if (len < 0.01f) {
        // Touching only diagonally, or wedged evenly: count as a floor if
        // anything solid is around
        bool any = false;
        for (int dy = -1; dy <= 1 && !any; ++dy)
          for (int dx = -1; dx <= 1 && !any; ++dx)
            any = (dx || dy) && Solid(sim, x + dx, y + dy);
        if (!any)
          continue;
        n = {0.0f, -1.0f};
        len = 1.0f;
      }
      float d = Dist2(at, cursor);
      if (d < bestD) {
        bestD = d;
        best = Spot{at, {n.x / len, n.y / len}};
      }
    }
  }
  return best;
}

} // namespace Placement

std::optional<CastTargeting::Target>
CastTargeting::Update(const Simulation &sim, Vector2 caster, Vector2 mouse,
                      bool worldInput, int facing, const char **blocked) {
  *blocked = nullptr;
  bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  bool right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
  if (!worldInput || (!left && !right))
    return std::nullopt;
  Vector2 d{mouse.x - caster.x, mouse.y - caster.y};
  float len = std::hypot(d.x, d.y);
  Vector2 aim = len > 0.5f ? Vector2{d.x / len, d.y / len}
                           : Vector2{static_cast<float>(facing), 0.0f};
  if (left)
    return Target{aim, std::nullopt};
  std::optional<Placement::Spot> spot = Placement::FindSurface(sim, mouse, caster);
  if (!spot) {
    *blocked = "No ground or wall within reach of the cursor to draw on";
    return std::nullopt;
  }
  if (aim.x * spot->normal.x + aim.y * spot->normal.y < 0.0f)
    aim = spot->normal;
  return Target{aim, spot->pos};
}

void CastTargeting::DrawWorld(const Simulation &sim, Vector2 caster,
                              Vector2 mouse) {
  std::optional<Placement::Spot> spot = Placement::FindSurface(sim, mouse, caster);
  if (!spot)
    return;
  Vector2 at{spot->pos.x * CELL_SIZE, spot->pos.y * CELL_SIZE};
  Color ink{214, 180, 110, 150};
  DrawCircleLinesV(at, 2.6f * CELL_SIZE, ink);
  DrawCircleLinesV(at, 1.8f * CELL_SIZE, ink);
  DrawText("R", static_cast<int>(at.x + 3.2f * CELL_SIZE),
           static_cast<int>(at.y - 3.2f * CELL_SIZE), 10, ink);
}
