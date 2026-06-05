#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/erosion_system.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {
void UpdateWater(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // TODO: handle this also
  // TODO: handle wind affecting this bs also
  src.velocityY += GRAVITY;
  src.velocityY = std::min(src.velocityY, 5.0f);
  src.velocityX = std::clamp(src.velocityX, -3.0f, 3.0f);

  // Pressure from depth
  float depthPressure = 0.0f;
  for (int dy = 1; dy <= 20; ++dy) {
    int ny = y - dy;
    if (!ctx.currentGrid.InBounds(x, ny))
      break;
    if (ctx.currentGrid.GetCurrent(x, ny).element == Element::WATER)
      depthPressure += 0.5f;
    else
      break;
  }
  src.pressure = depthPressure;

  if (src.temperature >= 100.0f) {
    Cell steam = ElementFactory::Create(Element::STEAM);
    steam.temperature = src.temperature;
    MovementSystem::SetNext(x, y, steam, ctx);
    return;
  }

  if (src.temperature <= 0.0f) {
    Cell ice = ElementFactory::Create(Element::ICE);
    ice.temperature = src.temperature;
    MovementSystem::SetNext(x, y, ice, ctx);
    return;
  }

  bool moved = false;
  int steps = std::max(1, (int)std::abs(src.velocityY));

  // TODO: refactor this for multistep
  for (int s = 0; s < steps && !moved; ++s) {
    int ny = y + 1;
    if (ctx.currentGrid.InBounds(x, ny)) {
      const Cell &below = ctx.currentGrid.GetCurrent(x, ny);
      if (below.element == Element::AIR || below.element == Element::STEAM) {
        moved = MovementSystem::TryMove(x, y, x, ny, ctx);
      } else if (below.element == Element::EARTH) {
        moved = ErosionSystem::TryErode(x, y, x, ny, ctx);
        if (!moved)
          src.velocityY = 0;
      } else {
        src.velocityY = 0;
      }
    } else {
      src.velocityY = 0;
    }
  }

  if (!moved) {
    float spreadPower = 1.0f + src.pressure * 0.1f;
    int dirs[2] = {-1, 1};
    if (std::rand() % 2)
      std::swap(dirs[0], dirs[1]);

    for (int dir : dirs) {
      int tx = x + dir;
      if (ctx.currentGrid.InBounds(tx, y)) {
        const Cell &side = ctx.currentGrid.GetCurrent(tx, y);
        if (side.element == Element::AIR || side.element == Element::STEAM) {
          src.velocityX = dir * spreadPower;
          moved = MovementSystem::TryMove(x, y, tx, y, ctx);
          if (moved)
            break;
        } else if (side.element == Element::EARTH) {
          ErosionSystem::TryErode(x, y, tx, y, ctx);
        }
      }
    }
  }

  if (!moved) {
    src.velocityX *= 0.7f;
    src.velocityY = 0.0f;
    MovementSystem::SetNext(x, y, src, ctx);
  }
}
} // namespace Materials
