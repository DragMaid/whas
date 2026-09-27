#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

void UpdateEarth(int x, int y, ElementContext &ctx) {
  const auto& eConfig = ctx.config.earth;
  Cell src = ctx.grid.Get(x, y);

  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.grid.InBounds(nx, ny))
      continue;
    
    if (ctx.grid.Get(nx, ny).element == Element::FIRE) {
      src.hardness = std::max(0.0f, src.hardness - eConfig.fireHardnessLoss);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
