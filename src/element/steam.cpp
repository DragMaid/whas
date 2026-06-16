#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/element/base/properties.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

static bool HandleCondensation(int x, int y, Cell &src, const SteamConfig &sConfig, ElementContext &ctx) {
  // We use a bit of hysteresis here: boiling is 100, condensation is 90.
  // This prevents rapid oscillation if the temperature is hovering around 100.
  if (src.temperature <= sConfig.condensationTemp) {
    Cell water = ElementFactory::Create(Element::WATER, ctx.config);
    water.temperature = src.temperature;
    MovementSystem::SetNext(x, y, water, ctx);
    return true;
  }
  return false;
}

static bool HandleCloudFormation(int x, int y, Cell &src, const SteamConfig &sConfig, ElementContext &ctx) {
  if (y < GRID_H * sConfig.cloudFormationHeightRatio &&
      src.temperature > sConfig.cloudFormationTemp) {
    Cell cloud = ElementFactory::Create(Element::CLOUD, ctx.config);
    cloud.temperature = src.temperature - sConfig.cloudTempLoss;
    MovementSystem::SetNext(x, y, cloud, ctx);
    return true;
  }
  return false;
}

static void UpdateVelocity(Cell &src, const SteamConfig &sConfig) {
  // Buoyancy: hotter steam rises faster
  src.vy = sConfig.buoyancyBase - (src.temperature - sConfig.condensationTemp) * sConfig.buoyancyTempScale;
  
  // Drift: random horizontal movement
  src.vx += (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * sConfig.driftStrength;
  src.vx = std::clamp(src.vx, -sConfig.maxDrift, sConfig.maxDrift);
}

static bool TryBuoyancyMove(int x, int y, Cell &src, ElementContext &ctx) {
  float absVY = std::abs(src.vy);
  if (absVY < 0.1f) return false;

  int dir = (src.vy > 0.0f) ? 1 : -1;
  int steps = std::max(1, static_cast<int>(absVY));
  int furthestY = y;

  for (int s = 1; s <= steps; ++s) {
    int ty = y + s * dir;
    if (!ctx.grid.InBounds(x, ty)) break;
    
    const Cell &target = ctx.grid.Get(x, ty);
    if (MovementSystem::CanDisplace(src, target, ctx)) {
      furthestY = ty;
    } else {
      break;
    }
  }

  if (furthestY != y) {
    return MovementSystem::TryMove(x, y, x, furthestY, src, ctx);
  }
  return false;
}

static bool TryDriftMove(int x, int y, Cell &src, ElementContext &ctx) {
  float absVX = std::abs(src.vx);
  if (absVX < 0.1f) return false;

  int dir = (src.vx > 0.0f) ? 1 : -1;
  int steps = std::max(1, static_cast<int>(absVX));
  int furthestX = x;

  for (int s = 1; s <= steps; ++s) {
    int tx = x + s * dir;
    if (!ctx.grid.InBounds(tx, y)) break;

    const Cell &target = ctx.grid.Get(tx, y);
    if (MovementSystem::CanDisplace(src, target, ctx)) {
      furthestX = tx;
    } else {
      break;
    }
  }

  if (furthestX != x) {
    return MovementSystem::TryMove(x, y, furthestX, y, src, ctx);
  }
  return false;
}

void UpdateSteam(int x, int y, ElementContext &ctx) {
  const auto &sConfig = ctx.config.steam;
  Cell src = ctx.grid.Get(x, y);

  // if (HandleCondensation(x, y, src, sConfig, ctx)) return;
  if (HandleCloudFormation(x, y, src, sConfig, ctx)) return;

  UpdateVelocity(src, sConfig);

  bool moved = TryBuoyancyMove(x, y, src, ctx);
  if (!moved) {
    moved = TryDriftMove(x, y, src, ctx);
  }

  if (!moved) {
    MovementSystem::SetNext(x, y, src, ctx);
  }
}

} // namespace ElementsImpl
