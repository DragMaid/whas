#include "whas/physics/movement_system.h"
#include "whas/core/config.h"
#include "whas/element/base/factory.h"

bool MovementSystem::CanDisplace(const Cell &source, const Cell &target,
                                 const ElementContext &ctx) {
  if (source.element == target.element)
    return false;

  const auto &targetProps =
      ctx.config.elements[static_cast<size_t>(target.element)];
  return !targetProps.solid;
}

bool MovementSystem::TryMove(int x, int y, int tx, int ty, Cell &moved,
                             ElementContext &ctx) {
  if (!ctx.grid.InBounds(tx, ty))
    return false;
  Cell &target = ctx.grid.Get(tx, ty);
  if (target.lastUpdateFrame == ctx.frameIndex)
    return false;
  if (!CanDisplace(moved, target, ctx))
    return false;

  moved.lastUpdateFrame = ctx.frameIndex;

  if (target.element != Element::AIR) {
    Cell displaced = target;
    displaced.lastUpdateFrame = ctx.frameIndex;

    static constexpr int kOffsets[8][2] = {
        {0, -1},           // up
        {-1, -1}, {1, -1}, // up-left, up-right
        {-1, 0},  {1, 0},  // left, right
        {-1, 1},  {1, 1},  // down-left, down-right
        {0, 1},            // down (last resort)
    };

    bool pushed = false;

    for (const auto &off : kOffsets) {
      int nx = x + off[0], ny = y + off[1];
      if (!ctx.grid.InBounds(nx, ny))
        continue;

      Cell &neighbor = ctx.grid.Get(nx, ny);
      if (neighbor.element != Element::AIR)
        continue;

      bool hasEscapeRoute = false;
      for (int dy2 = -1; dy2 <= 1 && !hasEscapeRoute; ++dy2) {
        for (int dx2 = -1; dx2 <= 1; ++dx2) {
          if (dx2 == 0 && dy2 == 0)
            continue;
          int ex = nx + dx2, ey = ny + dy2;
          if (ex == x && ey == y)
            continue; // skip the cell we're vacating
          if (ctx.grid.InBounds(ex, ey) &&
              ctx.grid.Get(ex, ey).element == Element::AIR) {
            hasEscapeRoute = true;
            break;
          }
        }
      }

      if (!hasEscapeRoute)
        continue;

      neighbor = displaced;
      pushed = true;
      neighbor.lastUpdateFrame = ctx.frameIndex - 1;
      ctx.chunks.WakeChunkAt(nx, ny, ctx.frameIndex);
      break;
    }

    if (!pushed) {
      return false;
    }

    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    air.lastUpdateFrame = ctx.frameIndex - 1;
    ctx.grid.Get(x, y) = air;
  } else {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    air.temperature = moved.temperature * 0.5f;
    air.lastUpdateFrame = ctx.frameIndex - 1;
    ctx.grid.Get(x, y) = air;
  }

  target = moved;
  ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex);
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
  return true;
}

void MovementSystem::SetNext(int x, int y, const Cell &c, ElementContext &ctx) {
  Cell &destination = ctx.grid.Get(x, y);
  destination = c;
  destination.lastUpdateFrame = ctx.frameIndex;
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
}

void MovementSystem::Carry(int x, int y, ElementContext &ctx) {
  Cell &c = ctx.grid.Get(x, y);
  c.lastUpdateFrame = ctx.frameIndex;
}
