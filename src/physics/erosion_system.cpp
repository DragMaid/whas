#include "whas/physics/erosion_system.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/properties.h"
#include "whas/physics/movement_system.h"
#include <cmath>

/*
 * @param wx, wy: coordinates of the supposed water cell
 * @param ex, ey: coordinates of the supposed unpassable cell
 */
bool ErosionSystem::TryErode(int wx, int wy, int ex, int ey,
                             ElementContext &ctx) {
  if (!ctx.currentGrid.InBounds(ex, ey))
    return false;

  Cell &supposed_w = ctx.currentGrid.GetCurrent(wx, wy);
  Cell &supposed_e = ctx.currentGrid.GetCurrent(ex, ey);

  if (ElementRegistry::GetProperties(supposed_e.element).passable)
    return false;

  float speed = std::sqrt(supposed_w.velocityX * supposed_w.velocityX +
                          supposed_w.velocityY * supposed_w.velocityY);

  float kineticEnergy = 0.5f * supposed_w.mass * speed * speed;

  if (kineticEnergy >= supposed_e.hardness) {
    // Update the breaking of earth block
    Cell air = ElementFactory::Create(Element::AIR);
    air.updated = true;
    ctx.currentGrid.GetNext(ex, ey) = air;

    // TODO: Update the new speed given the first break
    float remainingEnergy = kineticEnergy - supposed_e.hardness;
    float newSpeed = std::sqrt((2.0f * remainingEnergy) / supposed_w.mass);
    float scale = newSpeed / speed;
    supposed_w.velocityX *= scale;
    supposed_w.velocityY *= scale;

    return MovementSystem::TryMove(wx, wy, ex, ey, ctx);
  }

  return false;
}
