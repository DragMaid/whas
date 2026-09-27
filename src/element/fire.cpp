#include "whas/element/base/implementations.h"
#include <algorithm>
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {

void UpdateFire(int x, int y, ElementContext &ctx) {
  const auto& fConfig = ctx.config.fire;
  const auto &props = ctx.config.elements[static_cast<size_t>(Element::FIRE)];
  Cell src = ctx.grid.Get(x, y);

  // Combustion: while there's fuel left the fire makes its own heat
  if (props.defaultLifetime > 0.0f && src.lifetime > 0.0f) {
    float fuel = std::min(src.lifetime / props.defaultLifetime,
                          fConfig.maxFuelScale);
    src.temperature = std::max(
        src.temperature,
        fConfig.minTemp + (fConfig.burnTemp - fConfig.minTemp) * std::min(fuel, 1.0f));
  }

  // If the fire gets too cold, it dies and turns into air
  if (src.temperature < fConfig.minTemp) {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    MovementSystem::SetNext(x, y, air, ctx);
    return;
  }

  // Spontaneous Sparking
  if (std::rand() % fConfig.sparkChance == 0) {
    int uy = y - 1;
    if (ctx.grid.InBounds(x, uy)) {
      Cell spark = ElementFactory::Create(Element::FIRE, ctx.config);
      if (MovementSystem::CanDisplace(spark, ctx.grid.Get(x, uy), ctx)) {
        spark.temperature = src.temperature * fConfig.sparkTempScale;
        spark.lifetime = src.lifetime * fConfig.sparkLifetimeScale;
        MovementSystem::SetNext(x, uy, spark, ctx);
      }
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
