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
  // TODO: the impelementation of gravity for such small scale pixel
  // simulation can lead to super weird interaction, such as some droplets
  // falling way faster than others
  cell.velocityY = ctx.config.world.gravity;

  cell.velocityY = std::min(cell.velocityY, props.maxFallSpeed);

  cell.velocityX = std::clamp(cell.velocityX, -props.maxHorizontalSpeed,
                              props.maxHorizontalSpeed);
}

bool TryFall(int x, int y, Cell &cell, const LiquidProperties &props,
             ElementContext &ctx) {
  int steps = std::max(1, (int)std::round(cell.velocityY));
  int furthestY = y;

  for (int s = 1; s <= steps; ++s) {
    int nextY = y + s;

    if (!ctx.currentGrid.InBounds(x, nextY)) {
      cell.velocityY = 0.0f;
      break;
    }

    const Cell &target = ctx.currentGrid.GetCurrent(x, nextY);

    if (IsPassableForLiquid(target, props)) {
      furthestY = nextY;
      continue;
    }

    if (props.canErodeTerrain && target.element == Element::EARTH) {

      if (ErosionSystem::TryErode(x, y, x, nextY, cell, ctx))
        return true;

      cell.velocityY = 0.0f;
    }

    break;
  }

  if (furthestY == y)
    return false;

  return MovementSystem::TryMove(x, y, x, furthestY, cell, ctx);
}

float ComputeSpreadPower(int x, int y, const LiquidProperties &props,
                         ElementContext &ctx) {
  float pressure = PressureSystem::GetPressure(x, y, ctx.currentGrid);

  return (1.0f + pressure * props.spreadFactor) * (1.0f - props.viscosity);
}

bool TrySpreadDirection(int x, int y, int dir, float spreadPower, Cell &cell,
                        const LiquidProperties &props, ElementContext &ctx) {
  int spreadSteps = std::max(1, (int)std::round(spreadPower));

  int furthestX = x;

  for (int s = 1; s <= spreadSteps; ++s) {
    int nextX = x + dir * s;

    if (!ctx.currentGrid.InBounds(nextX, y))
      break;

    const Cell &target = ctx.currentGrid.GetCurrent(nextX, y);

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

  cell.velocityX *= props.friction;

  return MovementSystem::TryMove(x, y, furthestX, y, cell, ctx);
}

bool TrySpread(int x, int y, Cell &cell, const LiquidProperties &props,
               ElementContext &ctx) {
  float spreadPower = ComputeSpreadPower(x, y, props, ctx);

  int dirs[2] = {-1, 1};

  if (ctx.rng() % 2)
    std::swap(dirs[0], dirs[1]);

  for (int dir : dirs) {
    if (TrySpreadDirection(x, y, dir, spreadPower, cell, props, ctx))
      return true;
  }

  return false;
}

void Settle(int x, int y, Cell &cell, const LiquidProperties &props,
            ElementContext &ctx) {
  cell.velocityX *= props.friction;
  cell.velocityY = 0.0f;

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
