#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/element/base/properties.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

struct SteamProps {
    float condensationTemp = 90.0f;
    float cloudFormationHeightRatio = 0.25f; // GRID_H / 4
    float cloudFormationTemp = 95.0f;
    float cloudTempLoss = 50.0f;
    float buoyancyBase = -1.5f;
    float buoyancyTempScale = 0.01f;
    float driftStrength = 0.1f;
    float maxDrift = 1.0f;
};

static const SteamProps LocalSteamProps;

void UpdateSteam(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // Note: Lifetime decay is now handled by the registry
  
  // Condensation
  if (src.temperature <= LocalSteamProps.condensationTemp) {
    Cell water = ElementFactory::Create(Element::WATER);
    water.temperature = src.temperature;
    MovementSystem::SetNext(x, y, water, ctx);
    return;
  }

  // Cloud Formation
  if (y < GRID_H * LocalSteamProps.cloudFormationHeightRatio && 
      src.temperature > LocalSteamProps.cloudFormationTemp) {
    Cell cloud = ElementFactory::Create(Element::CLOUD);
    cloud.temperature = src.temperature - LocalSteamProps.cloudTempLoss;
    MovementSystem::SetNext(x, y, cloud, ctx);
    return;
  }

  // Physics: Buoyancy and Drift
  src.velocityY = LocalSteamProps.buoyancyBase - (src.temperature - LocalSteamProps.condensationTemp) * LocalSteamProps.buoyancyTempScale;
  src.velocityX += (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * LocalSteamProps.driftStrength;
  src.velocityX = std::clamp(src.velocityX, -LocalSteamProps.maxDrift, LocalSteamProps.maxDrift);

  bool moved = false;
  
  // Attempt upward movement
  int uy = y - 1;
  if (ctx.currentGrid.InBounds(x, uy)) {
    const auto &props = ElementRegistry::GetProperties(ctx.currentGrid.GetCurrent(x, uy).element);
    if (props.passable)
      moved = MovementSystem::TryMove(x, y, x, uy, ctx);
  }

  // Attempt sideways movement if upward fails
  if (!moved) {
    int dirs[2] = {-1, 1};
    if (std::rand() % 2) std::swap(dirs[0], dirs[1]);
    for (int dir : dirs) {
      int tx = x + dir;
      if (ctx.currentGrid.InBounds(tx, y)) {
        const auto &props = ElementRegistry::GetProperties(ctx.currentGrid.GetCurrent(tx, y).element);
        if (props.passable) {
          moved = MovementSystem::TryMove(x, y, tx, y, ctx);
          if (moved) break;
        }
      }
    }
  }

  if (!moved)
    MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
