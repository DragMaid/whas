#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/element/base/properties.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {
void UpdateSteam(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);
  src.temperature -= 0.5f;
  src.lifetime -= 0.016f;

  if (src.temperature <= 90.0f || src.lifetime <= 0.0f) {
    Cell water = ElementFactory::Create(Element::WATER);
    water.temperature = src.temperature;
    MovementSystem::SetNext(x, y, water, ctx);
    return;
  }

  if (y < GRID_H / 4 && src.temperature > 95.0f) {
    Cell cloud = ElementFactory::Create(Element::CLOUD);
    cloud.temperature = src.temperature - 50.0f;
    MovementSystem::SetNext(x, y, cloud, ctx);
    return;
  }

  src.velocityY = -1.5f - (src.temperature - 90.0f) * 0.01f;
  src.velocityX +=
      (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * 0.1f;
  src.velocityX = std::clamp(src.velocityX, -1.0f, 1.0f);

  bool moved = false;
  int uy = y - 1;
  if (ctx.currentGrid.InBounds(x, uy)) {
    const auto &props = ElementRegistry::GetProperties(
        ctx.currentGrid.GetCurrent(x, uy).element);
    if (props.passable)
      moved = MovementSystem::TryMove(x, y, x, uy, ctx);
  }

  if (!moved) {
    int dirs[2] = {-1, 1};
    if (std::rand() % 2)
      std::swap(dirs[0], dirs[1]);
    for (int dir : dirs) {
      int tx = x + dir;
      if (ctx.currentGrid.InBounds(tx, y)) {
        const auto &props = ElementRegistry::GetProperties(
            ctx.currentGrid.GetCurrent(tx, y).element);
        if (props.passable) {
          moved = MovementSystem::TryMove(x, y, tx, y, ctx);
          if (moved)
            break;
        }
      }
    }
  }

  if (!moved)
    MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace Materials
