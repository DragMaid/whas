#include "whas/physics/movement_system.h"
#include "whas/core/config.h"
#include "whas/element/base/factory.h"

// TODO: right now the velocity determine nothing
/*
 * @param x, y: coordinates of source cell
 * @param tx, ty: coordinates of target cell
 */
bool MovementSystem::TryMove(int x, int y, int tx, int ty, Cell &moved,
                             ElementContext &ctx) {
  if (!ctx.currentGrid.InBounds(tx, ty))
    return false;

  // Do not update twice
  Cell &targetNext = ctx.currentGrid.GetNext(tx, ty);
  if (targetNext.updated)
    return false;

  const Cell &targetCurrent = ctx.currentGrid.GetCurrent(tx, ty);

  // Do not move if the target block occupied is not passable (solid)
  const ElementProperties &targetProps =
      ctx.config.elements[static_cast<size_t>(targetCurrent.element)];
  if (!targetProps.passable)
    return false;

  moved.updated = true;

  if (targetCurrent.element != Element::AIR) {
    Cell displaced = targetCurrent;
    displaced.updated = true;
    ctx.currentGrid.GetNext(x, y) = displaced;
  } else {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    // TODO: move this to another also
    air.temperature = moved.temperature * 0.5f;
    air.updated = true;
    ctx.currentGrid.GetNext(x, y) = air;
  }

  targetNext = moved;
  ctx.chunks.WakeChunkAt(tx, ty);
  ctx.chunks.WakeChunkAt(x, y);
  return true;
}

void MovementSystem::SetNext(int x, int y, const Cell &c, ElementContext &ctx) {
  Cell &destination = ctx.currentGrid.GetNext(x, y);
  destination = c;
  destination.updated = true;
  ctx.chunks.WakeChunkAt(x, y);
}

void MovementSystem::Carry(int x, int y, ElementContext &ctx) {
  Cell &destination = ctx.currentGrid.GetNext(x, y);
  if (!destination.updated) {
    destination = ctx.currentGrid.GetCurrent(x, y);
    destination.updated = true;
  }
}
