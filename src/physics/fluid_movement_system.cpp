#include "whas/physics/fluid_movement_system.h"
#include "whas/constants.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/erosion_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>
#include <cmath>

namespace FluidMovementSystem {

void UpdateLiquid(int x, int y, Cell &cell, const LiquidProperties &properties,
                  ElementContext &ctx) {
  // 1. Gravity and Velocity Integration
  cell.velocityY += ctx.config.world.gravity;
  cell.velocityY = std::min(cell.velocityY, properties.maxFallSpeed);
  cell.velocityX = std::clamp(cell.velocityX, -properties.maxHorizontalSpeed,
                              properties.maxHorizontalSpeed);

  bool moved = false;

  // 2. Multi-step Falling
  int steps = std::max(1, (int)std::abs(cell.velocityY));
  int furthestY = y;

  for (int s = 1; s <= steps; ++s) {
    int nextY = y + s;
    if (!ctx.currentGrid.InBounds(x, nextY)) {
      cell.velocityY = 0;
      break;
    }

    const Cell &target = ctx.currentGrid.GetCurrent(x, nextY);
    if (target.element == Element::AIR ||
        (properties.canDisplaceGas && target.element == Element::STEAM)) {
      furthestY = nextY;
    } else if (properties.canErodeTerrain && target.element == Element::EARTH) {
      if (ErosionSystem::TryErode(x, y, x, nextY, ctx)) {
        moved = true;
      } else {
        cell.velocityY = 0;
      }
      break;
    } else {
      cell.velocityY = 0;
      break;
    }
  }

  if (!moved && furthestY != y) {
    moved = MovementSystem::TryMove(x, y, x, furthestY, ctx);
  }

  // 3. Sideways Spreading
  if (!moved) {
    float pressure = PressureSystem::GetPressure(x, y, ctx.currentGrid);
    float spreadPower = (1.0f + pressure * properties.spreadFactor) * (1.0f - properties.viscosity);
    int spreadSteps = std::max(1, (int)std::round(spreadPower));

    int dirs[2] = {-1, 1};
    if (ctx.rng() % 2)
      std::swap(dirs[0], dirs[1]);

    for (int dir : dirs) {
      int furthestX = x;
      for (int s = 1; s <= spreadSteps; ++s) {
        int nextX = x + s * dir;
        if (!ctx.currentGrid.InBounds(nextX, y)) break;

        const Cell &side = ctx.currentGrid.GetCurrent(nextX, y);
        if (side.element == Element::AIR || (properties.canDisplaceGas && side.element == Element::STEAM)) {
          furthestX = nextX;
        } else if (properties.canErodeTerrain && side.element == Element::EARTH) {
          if (ErosionSystem::TryErode(x, y, nextX, y, ctx)) {
             moved = true;
          }
          break;
        } else {
          break;
        }
      }

      if (!moved && furthestX != x) {
        cell.velocityX = dir * spreadPower;
        moved = MovementSystem::TryMove(x, y, furthestX, y, ctx);
        if (moved) break;
      }
    }
  }

  // 4. Final State Update
  if (!moved) {
    cell.velocityX *= properties.friction; 
    cell.velocityY = 0.0f;
    MovementSystem::SetNext(x, y, cell, ctx);
  }
}

} // namespace FluidMovementSystem
