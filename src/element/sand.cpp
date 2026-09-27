#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/movement_system.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace ElementsImpl {

void UpdateSand(int x, int y, ElementContext &ctx) {
  Cell sand = ctx.grid.Get(x, y);

  // Apply Gravity
  sand.vy += ctx.config.world.gravity;
  sand.vy = std::min(sand.vy, 10.0f); // Max fall speed

  int steps = std::max(1, (int)std::floor(sand.vy));
  int furthestY = y;
  
  for (int s = 1; s <= steps; ++s) {
    int nextY = y + s;
    if (!ctx.grid.InBounds(x, nextY)) {
        sand.vy = 0.0f;
        break;
    }
    
    const Cell &target = ctx.grid.Get(x, nextY);
    if (MovementSystem::CanDisplace(sand, target, ctx)) {
        furthestY = nextY;
    } else {
        sand.vy = 0.0f;
        break;
    }
  }

  bool moved = false;
  if (furthestY > y) {
    // Try the furthest cell first; if contention occurs, try shorter distances
    // so the grain can still progress this frame.
    for (int ty = furthestY; ty > y; --ty) {
      if (MovementSystem::TryMove(x, y, x, ty, sand, ctx)) {
        moved = true;
        break;
      }
    }
  }

  if (!moved) {
    // Try move diagonal down (sliding)
    std::vector<int> dirs = {-1, 1};
    std::shuffle(dirs.begin(), dirs.end(), ctx.rng);

    for (int dx : dirs) {
      if (MovementSystem::TryMove(x, y, x + dx, y + 1, sand, ctx)) {
        return;
      }
    }

    // If not moved, mark as updated and dampen velocity
    sand.vy *= 0.5f;
    MovementSystem::SetNext(x, y, sand, ctx);
  }
}

} // namespace ElementsImpl
