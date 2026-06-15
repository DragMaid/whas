#include "whas/physics/movement_system.h"
#include "whas/core/config.h"
#include "whas/element/base/factory.h"

bool MovementSystem::TryMove(int x, int y, int tx, int ty, Cell &moved,
                             ElementContext &ctx) {
  if (!ctx.grid.InBounds(tx, ty))
    return false;

  Cell &target = ctx.grid.Get(tx, ty);
  
  // If target already updated this frame, we might have a collision with a moved particle
  // In single-buffer, we should be careful. 
  // For now, let's assume we can move into it if it's passable.
  if (target.lastUpdateFrame == ctx.frameIndex)
    return false;

  const ElementProperties &targetProps =
      ctx.config.elements[static_cast<size_t>(target.element)];
  if (!targetProps.passable)
    return false;

  moved.lastUpdateFrame = ctx.frameIndex;

  if (target.element != Element::AIR) {
    Cell displaced = target;
    displaced.lastUpdateFrame = ctx.frameIndex;
    ctx.grid.Get(x, y) = displaced;
  } else {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    air.temperature = moved.temperature * 0.5f;
    air.lastUpdateFrame = ctx.frameIndex;
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
