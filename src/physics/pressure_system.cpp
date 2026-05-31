#include "whas/physics/pressure_system.h"
#include "whas/constants.h"

void PressureSystem::Propagate(int x, int y, ElementContext &ctx) {
  Cell &source = ctx.currentGrid.GetCurrent(x, y);
  if (source.element != Element::WATER)
    return;

  const int dx[2] = {-1, 1};
  for (int direction : dx) {
    int nx = x + direction;
    if (!ctx.currentGrid.InBounds(nx, y)) continue;

    Cell& target = ctx.currentGrid.GetNext(nx, y);
    // TODO: re-consider this later
    // TODO: can consider expanding for any liquid
    if (target.element == Element::WATER) {
        float diff = source.pressure - target.pressure;
        target.pressure += diff * PRESSURE_EQ;
        source.pressure -= diff * PRESSURE_EQ;
    }
  }
}
