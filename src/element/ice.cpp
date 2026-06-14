#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {

void UpdateIce(int x, int y, ElementContext &ctx) {
  const auto& iConfig = ctx.config.ice;
  Cell src = ctx.grid.Get(x, y);
// Melting logic
if (src.temperature > iConfig.meltPoint) {
  if (std::rand() % iConfig.meltChance == 0) {
    Cell water = ElementFactory::Create(Element::WATER, ctx.config);
    water.temperature = src.temperature;
    MovementSystem::SetNext(x, y, water, ctx);
    return;
  }
}


  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.grid.InBounds(nx, ny))
      continue;
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
