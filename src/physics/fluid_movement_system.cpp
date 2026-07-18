#include "whas/physics/fluid_movement_system.h"
#include "whas/physics/erosion_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>
#include <cmath>

namespace FluidMovementSystem {

namespace {

// TODO: consider evey sub function just setting the velocity which then gets executed by movement system
// TODO: fix the middle collapse for some reason
void IntegrateVelocity(Cell &cell, const LiquidProperties &props,
                       ElementContext &ctx) {
  cell.vy += ctx.config.world.gravity;
  cell.vy = std::min(cell.vy, props.maxFallSpeed);

  // Continuous horizontal drag.
  constexpr float kAirDrag = 0.85f;

  cell.vx *= kAirDrag;

  if (std::abs(cell.vx) < 0.05f)
    cell.vx = 0.0f;

  cell.vx =
      std::clamp(cell.vx, -props.maxHorizontalSpeed, props.maxHorizontalSpeed);
}

bool TryFall(int x, int y, Cell &cell, const LiquidProperties &props,
             ElementContext &ctx) {
  //int steps = std::max(1, (int)std::round(cell.vy));
  int steps = 1;
  int furthestY = y;

  for (int s = 1; s <= steps; ++s) {
    int nextY = y + s;

    if (!ctx.grid.InBounds(x, nextY)) {
      cell.vy = 0.0f;
      break;
    }

    const Cell &target = ctx.grid.Get(x, nextY);

    if (MovementSystem::CanDisplace(cell, target, ctx)) {
      furthestY = nextY;
      continue;
    }

    // TODO: the erosion can happen not only from falling alone
    if (props.canErodeTerrain && target.element == Element::EARTH) {
      if (ErosionSystem::TryErode(x, y, x, nextY, cell, ctx))
        return true;
    }

    float impactSpeed = cell.vy;

    //if (impactSpeed > 0.25f) {
    //  int dir;

    //  if (std::abs(cell.vx) > 0.25f) {
    //    // 60% keep direction
    //    // 40% randomize

    //    bool keepDirection = (ctx.rng() % 100) < 60;

    //    if (keepDirection)
    //      dir = (cell.vx > 0.0f) ? 1 : -1;
    //    else
    //      dir = (ctx.rng() % 2) ? 1 : -1;
    //  } else {
    //    dir = (ctx.rng() % 2) ? 1 : -1;
    //  }

    //  constexpr float kImpactTransfer = 0.25f;

    //  cell.vx += dir * impactSpeed * kImpactTransfer * (1.0f - props.viscosity);

    //  cell.vx = std::clamp(cell.vx, -props.maxHorizontalSpeed,
    //                       props.maxHorizontalSpeed);
    //}

    //cell.vy = 0.0f;
    break;
  }

  if (furthestY == y)
    return false;

  // Attempt to move to the furthest found cell; if that fails (contention),
  // fall back to shorter distances so the element can still advance this frame.
  for (int ty = furthestY; ty > y; --ty) {
    if (MovementSystem::TryMove(x, y, x, ty, cell, ctx))
      return true;
  }

  return false;
}

float ComputeSpreadPower(int x, int y, const Cell &cell,
                         const LiquidProperties &props, ElementContext &ctx) {
  float pressure = PressureSystem::GetPressure(x, y, ctx.grid);

  float pressureForce = pressure * props.spreadFactor;

  float momentumForce = std::abs(cell.vx) * 0.35f;

  float spreadPower = pressureForce + momentumForce;

  return std::clamp(spreadPower, 1.0f, 3.0f);
}

bool TrySpreadDirection(int x, int y, int dir, float spreadPower, Cell &cell,
                        const LiquidProperties &props, ElementContext &ctx) {
  int spreadSteps = std::clamp((int)std::round(spreadPower), 1, 3);
  int furthestX = x;

  for (int s = 1; s <= spreadSteps; ++s) {
    int nextX = x + dir * s;

    if (!ctx.grid.InBounds(nextX, y))
      break;

    const Cell &target = ctx.grid.Get(nextX, y);

    if (MovementSystem::CanDisplace(cell, target, ctx)) {
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

  return MovementSystem::TryMove(x, y, furthestX, y, cell, ctx);
}

bool TrySlide(int x, int y, Cell &cell, const LiquidProperties &props,
              ElementContext &ctx) {
  int dirs[2] = {-1, 1};
  if (ctx.rng() % 2)
    std::swap(dirs[0], dirs[1]);

  for (int dir : dirs) {
    int nx = x + dir;
    int ny = y + 1;

    if (ctx.grid.InBounds(nx, ny)) {
      const Cell &target = ctx.grid.Get(nx, ny);
      if (MovementSystem::CanDisplace(cell, target, ctx)) {
        if (MovementSystem::TryMove(x, y, nx, ny, cell, ctx))
          return true;
      }
    }
  }
  return false;
}

void BuildSpreadDirections(const Cell &cell, int (&dirs)[2],
                           ElementContext &ctx) {
  dirs[0] = -1;
  dirs[1] = 1;

  float speed = std::abs(cell.vx);

  if (speed < 0.25f) {
    if (ctx.rng() % 2)
      std::swap(dirs[0], dirs[1]);

    return;
  }

  int preferred = (cell.vx > 0.0f) ? 1 : -1;

  bool followMomentum = (ctx.rng() % 100) < 65;

  if (followMomentum) {
    dirs[0] = preferred;
    dirs[1] = -preferred;
  } else {
    dirs[0] = -preferred;
    dirs[1] = preferred;
  }
}

bool TrySpread(int x, int y, Cell &cell, const LiquidProperties &props,
               ElementContext &ctx) {
  float pressure = PressureSystem::GetPressure(x, y, ctx.grid);

  if (pressure < 0.05f && std::abs(cell.vx) < 0.1f) {
    return false;
  }

  float spreadPower = ComputeSpreadPower(x, y, cell, props, ctx);

  int dirs[2];
  BuildSpreadDirections(cell, dirs, ctx);

  for (int dir : dirs) {
    if (TrySpreadDirection(x, y, dir, spreadPower, cell, props, ctx)) {
      return true;
    }
  }

  return false;
}

void Settle(int x, int y, Cell &cell, const LiquidProperties &props,
            ElementContext &ctx) {
  cell.vx *= 0.7f;

  if (std::abs(cell.vx) < 0.05f)
    cell.vx = 0.0f;

  cell.vy *= 0.25f;

  MovementSystem::SetNext(x, y, cell, ctx);
}

} // namespace

void UpdateLiquid(int x, int y, Cell &cell, const LiquidProperties &props,
                  ElementContext &ctx) {
  IntegrateVelocity(cell, props, ctx);

  if (TryFall(x, y, cell, props, ctx))
    return;

  // if (TrySlide(x, y, cell, props, ctx))
  //   return;

  // if (TrySpread(x, y, cell, props, ctx))
  //   return;

  Settle(x, y, cell, props, ctx);
}

} // namespace FluidMovementSystem
