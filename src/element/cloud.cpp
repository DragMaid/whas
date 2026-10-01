#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateCloud(int x, int y, ElementContext &ctx) {
  const auto &cConfig = ctx.config.cloud;
  Cell src = ctx.grid.Get(x, y);

  // Cloud disperse
  if (src.moisture <= cConfig.minMoisture) {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    MovementSystem::SetNext(x, y, air, ctx);
    return;
  }

  // Wind Jitter & Horizontal Drift
  float randomJitter =
      ctx.rng.Unit() * 2.0f - 1.0f;
  src.vx += randomJitter * cConfig.windJitter;
  src.vx =
      std::clamp(src.vx, -cConfig.maxDrift, cConfig.maxDrift);

  // Ambient Rain Generation
  if (ctx.rng.Below(cConfig.rainChance) == 0) {
    int ry = y + 1;
    if (ctx.grid.InBounds(x, ry)) {
      Cell rain = ElementFactory::Create(Element::WATER, ctx.config);
      if (MovementSystem::CanDisplace(rain, ctx.grid.Get(x, ry), ctx)) {
        rain.vy = cConfig.rainVelocity;
        MovementSystem::SetNext(x, ry, rain, ctx);
        if (!cConfig.infiniteRain)
          src.moisture -= cConfig.rainMoistureCost;
      }
    }
  }

  // Execution of Drift Movement
  bool moved = false;
  float absVX = std::abs(src.vx);
  if (absVX >= cConfig.moveThreshold) {
    int dir = (src.vx > 0.0f) ? 1 : -1;
    int steps = std::max(1, static_cast<int>(absVX));
    int furthestX = x;

    for (int s = 1; s <= steps; ++s) {
      int tx = x + s * dir;
      if (!ctx.grid.InBounds(tx, y))
        break;

      const Cell &target = ctx.grid.Get(tx, y);
      if (MovementSystem::CanDisplace(src, target, ctx)) {
        furthestX = tx;
      } else {
        break;
      }
    }

    if (furthestX != x) {
      moved = MovementSystem::TryMove(x, y, furthestX, y, src, ctx);
    }
  }

  if (!moved) {
    MovementSystem::SetNext(x, y, src, ctx);
  }
}
} // namespace ElementsImpl
