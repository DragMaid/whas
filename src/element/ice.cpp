#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

struct IceProps {
    float meltPoint = 0.0f;
    int meltChance = 10;
    float fireHeatGain = 2.0f;
};

static const IceProps LocalIceProps;

void UpdateIce(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // Melting logic
  if (src.temperature > LocalIceProps.meltPoint) {
    if (std::rand() % LocalIceProps.meltChance == 0) {
      Cell water = ElementFactory::Create(Element::WATER);
      water.temperature = src.temperature;
      MovementSystem::SetNext(x, y, water, ctx);
      return;
    }
  }

  // Ice absorbs heat from surrounding Fire
  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;
    
    const Cell &nb = ctx.currentGrid.GetCurrent(nx, ny);
    if (nb.element == Element::FIRE) {
      src.temperature += LocalIceProps.fireHeatGain;
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
