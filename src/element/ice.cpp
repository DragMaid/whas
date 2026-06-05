#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateIce(int x, int y, ElementContext &ctx) {
  const auto& iConfig = ctx.config.ice;
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  if (src.temperature > iConfig.meltPoint) {
    if (std::rand() % iConfig.meltChance == 0) {
      Cell water = ElementFactory::Create(Element::WATER);
      water.temperature = src.temperature;
      MovementSystem::SetNext(x, y, water, ctx);
      return;
    }
  }

  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;
    
    const Cell &nb = ctx.currentGrid.GetCurrent(nx, ny);
    if (nb.element == Element::FIRE) {
      src.temperature += iConfig.fireHeatGain;
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
