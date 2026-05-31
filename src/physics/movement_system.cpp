#include "whas/physics/movement_system.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/properties.h"

/*
 * @param x, y: coordinates of source cell
 * @param tx, ty: coordinates of target cell
 */
bool MovementSystem::TryMove(int x, int y, int tx, int ty,
                             ElementContext &ctx) {
  if (!ctx.currentGrid.InBounds(tx, ty))
    return false;

  // Do not update twice
  Cell &targetNext = ctx.currentGrid.GetNext(tx, ty);
  if (targetNext.updated)
    return false;

  const Cell &sourceCurrent = ctx.currentGrid.GetCurrent(x, y);
  const Cell &targetCurrent = ctx.currentGrid.GetCurrent(tx, ty);

  // Do not move if the target block occupied is not passable (solid)
  const ElementProperties &targetProps =
      ElementRegistry::GetProperties(targetCurrent.element);
  if (!targetProps.passable)
    return false;

  Cell moved = sourceCurrent;
  moved.updated = true;

  // If the target block was not air
  // this swap the location of the source and target block
  // if the target was air then replace old source with air
  // and reduce the temperature by half
  if (targetCurrent.element != Element::AIR) {
    Cell displaced = targetCurrent;
    displaced.updated = true;
    // Update the next state while current state is read-only
    ctx.currentGrid.GetNext(x, y) = displaced;
  } else {
    Cell air = ElementFactory::Create(Element::AIR);
    // TODO: set the displaced temperature to different rather than half
    // TODO: make sure air temperature goes down eventually
    air.temperature = sourceCurrent.temperature * 0.5f;
    air.updated = true;
    ctx.currentGrid.GetNext(x, y) = air;
  }

  targetNext = moved;
  // Wake both source and target chunks for update
  ctx.chunks.WakeChunkAt(tx, ty);
  ctx.chunks.WakeChunkAt(x, y);
  return true;
}

// Set the next state and update the chunk for update
void MovementSystem::SetNext(int x, int y, const Cell &c, ElementContext &ctx) {
  Cell &destination = ctx.currentGrid.GetNext(x, y);
  destination = c;
  destination.updated = true;
  ctx.chunks.WakeChunkAt(x, y);
}

// Continue to bring the current state to next
void MovementSystem::Carry(int x, int y, ElementContext &ctx) {
  Cell &destination = ctx.currentGrid.GetNext(x, y);
  if (!destination.updated) {
    destination = ctx.currentGrid.GetCurrent(x, y);
    destination.updated = true;
  }
}
