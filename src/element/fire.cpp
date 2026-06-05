#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

struct FireProps {
    float coolingRate = 2.0f;
    float minTemp = 200.0f;
    int sparkChance = 4;
    float sparkTempScale = 0.6f;
    float sparkLifetimeScale = 0.5f;
};

static const FireProps LocalFireProps;

void UpdateFire(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // Note: Lifetime decay is now handled by the registry

  src.temperature = std::max(LocalFireProps.minTemp, src.temperature - LocalFireProps.coolingRate);

  // Spontaneous Sparking
  if (std::rand() % LocalFireProps.sparkChance == 0) {
    int uy = y - 1;
    if (ctx.currentGrid.InBounds(x, uy) &&
        ctx.currentGrid.GetCurrent(x, uy).element == Element::AIR) {
      Cell spark = ElementFactory::Create(Element::FIRE);
      spark.temperature = src.temperature * LocalFireProps.sparkTempScale;
      spark.lifetime = src.lifetime * LocalFireProps.sparkLifetimeScale;
      MovementSystem::SetNext(x, uy, spark, ctx);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
