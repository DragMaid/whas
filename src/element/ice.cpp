#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {
void UpdateIce(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  if (src.temperature > 0.0f) {
    // Gradually melt
    if (std::rand() % 10 == 0) {
      Cell water = ElementFactory::Create(Element::WATER);
      water.temperature = src.temperature;
      MovementSystem::SetNext(x, y, water, ctx);
      return;
    }
  }

  // TODO: not really needed, instead think about how ice heat transfer can
  // essentially also cool water down to freezing point
  // Fire heats ice up significantly (handled by HeatSystem mostly, but can add
  // direct cooling here)
  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;
    const Cell &nb = ctx.currentGrid.GetCurrent(nx, ny);
    if (nb.element == Element::FIRE) {
      src.temperature += 2.0f;
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace Materials
