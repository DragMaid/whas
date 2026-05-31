#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {
void UpdateFire(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);
  src.lifetime -= 0.016f;

  if (src.lifetime <= 0.0f) {
    Cell hot = ElementFactory::Create(Element::AIR);
    hot.temperature = src.temperature * 0.3f;
    MovementSystem::SetNext(x, y, hot, ctx);
    return;
  }

  src.temperature = std::max(200.0f, src.temperature - 2.0f);

  const int dx4[] = {0, 0, -1, 1};
  const int dy4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + dx4[i], ny = y + dy4[i];
    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;
    const Cell &nb = ctx.currentGrid.GetCurrent(nx, ny);

    // TODO: remove all these magic numbers also
    // TODO: make use of the temperature system, it doesn't just get put off
    // faster because its ice
    if (nb.element == Element::WATER) {
      float cooling = 300.0f;
      if (cooling >= src.temperature) {
        MovementSystem::SetNext(x, y, ElementFactory::Create(Element::AIR),
                                ctx);
        return;
      } else {
        src.temperature -= 50.0f;
      }
    } else if (nb.element == Element::ICE) {
      src.temperature -= 100.0f; // Ice cools fire a lot
    }
  }

  if (std::rand() % 4 == 0) {
    int uy = y - 1;
    if (ctx.currentGrid.InBounds(x, uy) &&
        ctx.currentGrid.GetCurrent(x, uy).element == Element::AIR) {
      Cell spark = ElementFactory::Create(Element::FIRE);
      spark.temperature = src.temperature * 0.6f;
      spark.lifetime = src.lifetime * 0.5f;
      MovementSystem::SetNext(x, uy, spark, ctx);
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace Materials
