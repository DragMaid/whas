#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {
void UpdateEarth(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;
    // TODO: re-consider this weird way of handling it
    // TODO: rather than making fire affects it, make anything with
    // hot temperature be able to affect earth better
    if (ctx.currentGrid.GetCurrent(nx, ny).element == Element::FIRE) {
      src.hardness = std::max(0.0f, src.hardness - 0.5f);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace Materials
