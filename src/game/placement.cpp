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
  auto toward = [facing](Vector2 from, Vector2 to, Vector2 fallback) {
    Vector2 d{to.x - from.x, to.y - from.y};
    float len = std::hypot(d.x, d.y);
    if (len > 1.0f)
      return Vector2{d.x / len, d.y / len};
    return fallback.x == 0.0f && fallback.y == 0.0f
               ? Vector2{static_cast<float>(facing), 0.0f}
               : fallback;
  };

  if (!m_placing) {
    if (!worldInput || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
      return std::nullopt;
    return Target{toward(caster, mouse, {}), std::nullopt};
  }

  if (m_held) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      m_held.reset(); // changed their mind
      return std::nullopt;
    }
    if (!IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
      return std::nullopt;
    Placement::Spot spot = *m_held;
    m_held.reset();
    return Target{toward(spot.pos, mouse, spot.normal), spot.pos};
  }
  if (!worldInput || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    return std::nullopt;
  m_held = Placement::FindSurface(sim, mouse, caster);
  if (!m_held)
    *blocked = "No ground or wall within reach to draw on";
  return std::nullopt;
}

void CastTargeting::DrawWorld(const Simulation &sim, Vector2 caster,
                              Vector2 mouse) const {
  if (!m_placing)
    return;
  auto px = [](Vector2 v) { return Vector2{v.x * CELL_SIZE, v.y * CELL_SIZE}; };
  DrawCircleLinesV(px(caster), Placement::REACH * CELL_SIZE,
                   Color{214, 180, 110, 60});
  std::optional<Placement::Spot> spot =
      m_held ? m_held : Placement::FindSurface(sim, mouse, caster);
  if (!spot)
    return;
  Vector2 at = px(spot->pos);
  Color ink{214, 180, 110, 220};
  DrawCircleLinesV(at, 3.0f * CELL_SIZE, ink);
  DrawCircleLinesV(at, 2.2f * CELL_SIZE, ink);
  Vector2 dir = spot->normal;
  if (m_held) {
    Vector2 d{mouse.x - spot->pos.x, mouse.y - spot->pos.y};
    float len = std::hypot(d.x, d.y);
    if (len > 1.0f)
      dir = {d.x / len, d.y / len};
  }
  DrawLineEx(at, {at.x + dir.x * 8.0f * CELL_SIZE, at.y + dir.y * 8.0f * CELL_SIZE},
             2.0f, ink);
}
