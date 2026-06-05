#include "whas/physics/fluid_movement_system.h"

#include "whas/constants.h"
#include "whas/physics/erosion_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"

#include <algorithm>
#include <cmath>

namespace FluidMovementSystem {

namespace {

bool IsPassable(const Cell &target, const LiquidProperties &properties) {
  return target.element == Element::AIR ||
         (properties.canDisplaceGas && target.element == Element::STEAM);
}

void IntegrateVelocity(Cell &cell, const LiquidProperties &properties) {
  cell.velocityY += GRAVITY;

  cell.velocityY = std::min(cell.velocityY, properties.maxFallSpeed);

  cell.velocityX = std::clamp(cell.velocityX, -properties.maxHorizontalSpeed,
                              properties.maxHorizontalSpeed);
}

bool TryFall(int x, int y, Cell &cell, const LiquidProperties &properties,
             ElementContext &ctx) {
  const int maxSteps = std::max(1, static_cast<int>(std::abs(cell.velocityY)));

  int furthestY = y;

  for (int step = 1; step <= maxSteps; ++step) {
    const int nextY = y + step;

    if (!ctx.currentGrid.InBounds(x, nextY)) {
      cell.velocityY = 0.0f;
      break;
    }

    const Cell &target = ctx.currentGrid.GetCurrent(x, nextY);

    if (IsPassable(target, properties)) {
      furthestY = nextY;
      continue;
    }

    if (properties.canErodeTerrain && target.element == Element::EARTH) {

      if (ErosionSystem::TryErode(x, y, x, nextY, ctx)) {
        return true;
      }

      cell.velocityY = 0.0f;
      break;
    }

    cell.velocityY = 0.0f;
    break;
  }

  if (furthestY == y)
    return false;

  return MovementSystem::TryMove(x, y, x, furthestY, ctx);
}

int ComputeSpreadSteps(int x, int y, const LiquidProperties &properties,
                       ElementContext &ctx) {
  const float pressure = PressureSystem::GetPressure(x, y, ctx.currentGrid);

  const float spreadPower = (1.0f + pressure * properties.spreadFactor) *
                            (1.0f - properties.viscosity);

  return std::max(1, static_cast<int>(std::round(spreadPower)));
}

bool TrySpreadDirection(int x, int y, int dir, int spreadSteps, Cell &cell,
                        const LiquidProperties &properties,
                        ElementContext &ctx) {
  int furthestX = x;

  for (int step = 1; step <= spreadSteps; ++step) {
    const int nextX = x + dir * step;

    if (!ctx.currentGrid.InBounds(nextX, y))
      break;

    const Cell &target = ctx.currentGrid.GetCurrent(nextX, y);

    if (IsPassable(target, properties)) {
      furthestX = nextX;
      continue;
    }

    if (properties.canErodeTerrain && target.element == Element::EARTH) {

      return ErosionSystem::TryErode(x, y, nextX, y, ctx);
    }

    break;
  }

  if (furthestX == x)
    return false;

  cell.velocityX = static_cast<float>(dir * spreadSteps);

  return MovementSystem::TryMove(x, y, furthestX, y, ctx);
}

bool TrySpread(int x, int y, Cell &cell, const LiquidProperties &properties,
               ElementContext &ctx) {
  const int spreadSteps = ComputeSpreadSteps(x, y, properties, ctx);

  int directions[2] = {-1, 1};

  if (ctx.rng() % 2)
    std::swap(directions[0], directions[1]);

  for (int dir : directions) {
    if (TrySpreadDirection(x, y, dir, spreadSteps, cell, properties, ctx)) {
      return true;
    }
  }

  return false;
}

void ApplyRestingState(int x, int y, Cell &cell, ElementContext &ctx) {
  cell.velocityX *= WATER_FRICTION;
  cell.velocityY = 0.0f;

  MovementSystem::SetNext(x, y, cell, ctx);
}

} // namespace

void UpdateLiquid(int x, int y, Cell &cell, const LiquidProperties &properties,
                  ElementContext &ctx) {
  IntegrateVelocity(cell, properties);

  if (TryFall(x, y, cell, properties, ctx)) {
    return;
  }

  if (TrySpread(x, y, cell, properties, ctx)) {
    return;
  }

  ApplyRestingState(x, y, cell, ctx);
}

} // namespace FluidMovementSystem
