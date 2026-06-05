#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateFire(int x, int y, ElementContext &ctx) {
  const auto& fConfig = ctx.config.fire;
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  src.temperature = std::max(fConfig.minTemp, src.temperature - fConfig.coolingRate);

  // Spontaneous Sparking
  if (std::rand() % fConfig.sparkChance == 0) {
    int uy = y - 1;
    if (ctx.currentGrid.InBounds(x, uy) &&
        ctx.currentGrid.GetCurrent(x, uy).element == Element::AIR) {
      Cell spark = ElementFactory::Create(Element::FIRE);
      spark.temperature = src.temperature * fConfig.sparkTempScale;
      spark.lifetime = src.lifetime * fConfig.sparkLifetimeScale;
      MovementSystem::SetNext(x, uy, spark, ctx);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
