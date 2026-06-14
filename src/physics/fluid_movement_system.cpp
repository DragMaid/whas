#include "whas/physics/fluid_movement_system.h"
#include "whas/physics/erosion_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>
#include <cmath>

namespace FluidMovementSystem {

namespace {

bool IsPassableForLiquid(const Cell &target, const LiquidProperties &props) {
  if (target.element == Element::AIR || target.element == Element::FIRE)
    return true;

  if (props.canDisplaceGas &&
      (target.element == Element::STEAM || target.element == Element::CLOUD))
    return true;

  return false;
}

void IntegrateVelocity(Cell &cell, const LiquidProperties &props,
                       ElementContext &ctx) {
  // Accumulate downward velocity from gravity
  cell.vy += ctx.config.world.gravity;
  cell.vy = std::min(cell.vy, props.maxFallSpeed);

  // Horizontal velocity clamp
  cell.vx = std::clamp(cell.vx, -props.maxHorizontalSpeed,
                        props.maxHorizontalSpeed);
}

bool TryFall(int x, int y, Cell &cell, const LiquidProperties &props,
             ElementContext &ctx) {
  int steps = std::max(1, (int)std::round(cell.vy));
  int furthestY = y;

  for (int s = 1; s <= steps; ++s) {
    int nextY = y + s;

    if (!ctx.grid.InBounds(x, nextY)) {
      cell.vy = 0.0f;
      break;
    }

    const Cell &target = ctx.grid.Get(x, nextY);

    if (IsPassableForLiquid(target, props)) {
      furthestY = nextY;
      continue;
    }

    if (props.canErodeTerrain && target.element == Element::EARTH) {
      if (ErosionSystem::TryErode(x, y, x, nextY, cell, ctx))
        return true;
    }

    cell.vy = 0.0f;
    break;
  }

  if (furthestY == y)
    return false;

  return MovementSystem::TryMove(x, y, x, furthestY, cell, ctx);
}

float ComputeSpreadPower(int x, int y, const Cell &cell,
                         const LiquidProperties &props, ElementContext &ctx) {
  float pressure = PressureSystem::GetPressure(x, y, ctx.grid);

  // Combine pressure and existing speed
  float drivingForce = pressure * props.spreadFactor + std::abs(cell.vx);

  if (drivingForce < 1.0f) {
    drivingForce = 1.0f;
  }

  return drivingForce * (1.0f - props.viscosity);
}

bool TrySpreadDirection(int x, int y, int dir, float spreadPower, Cell &cell,
                        const LiquidProperties &props, ElementContext &ctx) {
  int spreadSteps = std::max(1, (int)std::round(spreadPower));
  int furthestX = x;

  for (int s = 1; s <= spreadSteps; ++s) {
    int nextX = x + dir * s;

    if (!ctx.grid.InBounds(nextX, y))
      break;

    const Cell &target = ctx.grid.Get(nextX, y);

    if (IsPassableForLiquid(target, props)) {
      furthestX = nextX;
      continue;
    }

    if (props.canErodeTerrain && target.element == Element::EARTH) {
      return ErosionSystem::TryErode(x, y, nextX, y, cell, ctx);
    }

    break;
  }

  if (furthestX == x)
    return false;

  // Apply friction
  cell.vx *= props.friction;

  // Maintain momentum in the direction we moved
  if (std::abs(cell.vx) < 0.5f) {
    cell.vx = dir * 0.5f;
  }

  return MovementSystem::TryMove(x, y, furthestX, y, cell, ctx);
}

bool TrySpread(int x, int y, Cell &cell, const LiquidProperties &props,
               ElementContext &ctx) {
  float spreadPower = ComputeSpreadPower(x, y, cell, props, ctx);

  // Bias direction based on existing vx
  int firstDir = (cell.vx >= 0.0f) ? 1 : -1;
  int dirs[2] = {firstDir, -firstDir};

  // If vx is near zero, randomize
  if (std::abs(cell.vx) < 0.1f) {
    if (ctx.rng() % 2) std::swap(dirs[0], dirs[1]);
  }

  for (int dir : dirs) {
    if (TrySpreadDirection(x, y, dir, spreadPower, cell, props, ctx))
      return true;
  }

  return false;
}

void Settle(int x, int y, Cell &cell, const LiquidProperties &props,
            ElementContext &ctx) {
  cell.vx *= props.friction;
  if (std::abs(cell.vx) < 0.1f) {
    cell.vx = 0.0f;
  }

  cell.vy *= 0.5f; // Dampen vertical velocity when hitting something

  MovementSystem::SetNext(x, y, cell, ctx);
}

} // namespace

void UpdateLiquid(int x, int y, Cell &cell, const LiquidProperties &props,
                  ElementContext &ctx) {
  IntegrateVelocity(cell, props, ctx);

  if (TryFall(x, y, cell, props, ctx))
    return;

  if (TrySpread(x, y, cell, props, ctx))
    return;

  Settle(x, y, cell, props, ctx);
}

} // namespace FluidMovementSystem
