#include "whas/physics/erosion_system.h"
#include "whas/element/base/factory.h"
#include "whas/core/config.h"
#include "whas/physics/movement_system.h"
#include <cmath>

/*
 * @param wx, wy: coordinates of the supposed water cell
 * @param ex, ey: coordinates of the supposed unpassable cell
 */
bool ErosionSystem::TryErode(int wx, int wy, int ex, int ey, Cell &supposed_w,
                             ElementContext &ctx) {
  if (!ctx.grid.InBounds(ex, ey))
    return false;

  const Cell &supposed_e = ctx.grid.Get(ex, ey);

  const auto &props = ctx.config.elements[static_cast<size_t>(supposed_e.element)];
  if (props.passable)
    return false;

  float speed = std::sqrt(supposed_w.vx * supposed_w.vx +
                          supposed_w.vy * supposed_w.vy);

  float kineticEnergy = 0.5f * supposed_w.mass * speed * speed;

  if (kineticEnergy >= supposed_e.hardness) {
    // Update the breaking of earth block
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    ctx.grid.Get(ex, ey) = air;
    ctx.chunks.WakeChunkAt(ex, ey, ctx.frameIndex, true);

    float remainingEnergy = kineticEnergy - supposed_e.hardness;
    float newSpeed = std::sqrt((2.0f * remainingEnergy) / supposed_w.mass);
    float scale = (speed > 0.001f) ? newSpeed / speed : 0.0f;
    supposed_w.vx *= scale;
    supposed_w.vy *= scale;

    return MovementSystem::TryMove(wx, wy, ex, ey, supposed_w, ctx);
  }

  return false;
}
