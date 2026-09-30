#include "whas/element/base/econtext.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {

// Smoke drifts up one cell at a time, wandering sideways, and fades out
// through the lifetime system
void UpdateSmoke(int x, int y, ElementContext &ctx) {
  Cell src = ctx.grid.Get(x, y);

  int dx = static_cast<int>(ctx.rng.Below(3)) - 1;
  if (MovementSystem::TryMove(x, y, x + dx, y - 1, src, ctx))
    return;
  if (dx != 0 && MovementSystem::TryMove(x, y, x, y - 1, src, ctx))
    return;
  if (dx != 0 && MovementSystem::TryMove(x, y, x + dx, y, src, ctx))
    return;

  MovementSystem::SetNext(x, y, src, ctx);
}

} // namespace ElementsImpl
