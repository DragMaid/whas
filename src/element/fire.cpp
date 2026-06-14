#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {

void UpdateFire(int x, int y, ElementContext &ctx) {
  const auto& fConfig = ctx.config.fire;
  Cell src = ctx.grid.Get(x, y);

  // If the fire gets too cold, it dies and turns into air
  if (src.temperature < fConfig.minTemp) {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    MovementSystem::SetNext(x, y, air, ctx);
    return;
  }

  // Spontaneous Sparking
  if (std::rand() % fConfig.sparkChance == 0) {
    int uy = y - 1;
    if (ctx.grid.InBounds(x, uy) &&
        ctx.grid.Get(x, uy).element == Element::AIR) {
      Cell spark = ElementFactory::Create(Element::FIRE, ctx.config);
      spark.temperature = src.temperature * fConfig.sparkTempScale;
      spark.lifetime = src.lifetime * fConfig.sparkLifetimeScale;
      MovementSystem::SetNext(x, uy, spark, ctx);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
