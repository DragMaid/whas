#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateCloud(int x, int y, ElementContext &ctx) {
  const auto &cConfig = ctx.config.cloud;
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // 1. Cloud Dissipation / Heavy Rain Burst
  if (src.temperature <= cConfig.freezingPoint ||
      src.moisture <= cConfig.minMoisture) {
    if (std::rand() % cConfig.rainBurstChance == 0 &&
        src.moisture > cConfig.minMoisture) {
      int ry = y + 1;
      if (ctx.currentGrid.InBounds(x, ry) &&
          ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {
        Cell rain = ElementFactory::Create(Element::WATER);
        rain.velocityY = cConfig.rainBurstVelocity;
        MovementSystem::SetNext(x, ry, rain, ctx);
      }
    }
    MovementSystem::SetNext(x, y, ElementFactory::Create(Element::AIR), ctx);
    return;
  }

  // 2. Wind Jitter & Horizontal Drift
  float randomJitter =
      (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f -
      1.0f;
  src.velocityX += randomJitter * cConfig.windJitter;
  src.velocityX =
      std::clamp(src.velocityX, -cConfig.maxDrift, cConfig.maxDrift);

  // 3. Ambient Rain Generation
  if (std::rand() % cConfig.rainChance == 0) {
    int ry = y + 1;
    if (ctx.currentGrid.InBounds(x, ry) &&
        ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {
      Cell rain = ElementFactory::Create(Element::WATER);
      rain.velocityY = cConfig.rainVelocity;
      MovementSystem::SetNext(x, ry, rain, ctx);
      src.moisture -= cConfig.rainMoistureCost;
    }
  }

  // 4. Execution of Drift Movement
  bool moved = false;
  if (std::abs(src.velocityX) >= cConfig.moveThreshold) {
    int dir = (src.velocityX > 0.0f) ? 1 : -1;
    int tx = x + dir;

    if (ctx.currentGrid.InBounds(tx, y) &&
        ctx.currentGrid.GetCurrent(tx, y).element == Element::AIR) {
      moved = MovementSystem::TryMove(x, y, tx, y, ctx);
    }
  }

  if (!moved) {
    MovementSystem::SetNext(x, y, src, ctx);
  }
}
} // namespace ElementsImpl
