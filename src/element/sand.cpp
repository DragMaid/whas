#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/movement_system.h"
#include <algorithm>
#include <vector>

namespace ElementsImpl {

void UpdateSand(int x, int y, ElementContext &ctx) {
  Cell sand = ctx.grid.Get(x, y);

  // Try move down
  if (MovementSystem::TryMove(x, y, x, y + 1, sand, ctx)) {
    return;
  }

  // Try move diagonal down
  std::vector<int> dirs = {-1, 1};
  std::shuffle(dirs.begin(), dirs.end(), ctx.rng);

  for (int dx : dirs) {
    if (MovementSystem::TryMove(x, y, x + dx, y + 1, sand, ctx)) {
      return;
    }
  }

  // If not moved, mark as updated to prevent re-processing
  MovementSystem::Carry(x, y, ctx);
}

} // namespace ElementsImpl
