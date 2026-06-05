#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/element/base/properties.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateSteam(int x, int y, ElementContext &ctx) {
  const auto& sConfig = ctx.config.steam;
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // Condensation
  if (src.temperature <= sConfig.condensationTemp) {
    Cell water = ElementFactory::Create(Element::WATER);
    water.temperature = src.temperature;
    MovementSystem::SetNext(x, y, water, ctx);
    return;
  }

  // Cloud Formation
  if (y < GRID_H * sConfig.cloudFormationHeightRatio && 
      src.temperature > sConfig.cloudFormationTemp) {
    Cell cloud = ElementFactory::Create(Element::CLOUD);
    cloud.temperature = src.temperature - sConfig.cloudTempLoss;
    MovementSystem::SetNext(x, y, cloud, ctx);
    return;
  }

  // Physics: Buoyancy and Drift
  src.velocityY = sConfig.buoyancyBase - (src.temperature - sConfig.condensationTemp) * sConfig.buoyancyTempScale;
  src.velocityX += (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * sConfig.driftStrength;
  src.velocityX = std::clamp(src.velocityX, -sConfig.maxDrift, sConfig.maxDrift);

  bool moved = false;
  
  int uy = y - 1;
  if (ctx.currentGrid.InBounds(x, uy)) {
    const auto &props = ElementRegistry::GetProperties(ctx.currentGrid.GetCurrent(x, uy).element);
    if (props.passable)
      moved = MovementSystem::TryMove(x, y, x, uy, ctx);
  }

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
