#include "whas/physics/pressure_system.h"
#include "whas/constants.h"

void PressureSystem::Update(ElementContext &ctx) {
  auto &pressureBuffer = ctx.currentGrid.GetPressureBuffer();

  for (int x = 0; x < GRID_W; ++x) {
    float currentPressure = 0.0f;
    for (int y = 0; y < GRID_H; ++y) {
      const Cell &cell = ctx.currentGrid.GetCurrent(x, y);

      // TODO: add a state for water
      // For now, only water contributes to and receives pressure
      // This can be expanded to all liquids later
      if (cell.element == Element::WATER) {
        pressureBuffer[y * GRID_W + x] = currentPressure;
        currentPressure += PRESSURE_WEIGHT;

        // Cap pressure based on scan depth to maintain original behavior
        constexpr float MAX_PRESSURE = PRESSURE_SCAN_DEPTH * PRESSURE_WEIGHT;
        if (currentPressure > MAX_PRESSURE) {
          currentPressure = MAX_PRESSURE;
        }
      } else {
        pressureBuffer[y * GRID_W + x] = 0.0f;
        currentPressure = 0.0f;
      }
    }
  }
}

float PressureSystem::GetPressure(int x, int y, Grid &grid) {
  if (!grid.InBounds(x, y))
    return 0.0f;
  return grid.GetPressureBuffer()[y * GRID_W + x];
}

void PressureSystem::Propagate(int x, int y, ElementContext &ctx) {
  Cell &source = ctx.currentGrid.GetCurrent(x, y);
  if (source.element != Element::WATER)
    return;

  const int dx[2] = {-1, 1};
  for (int direction : dx) {
    int nx = x + direction;
    if (!ctx.currentGrid.InBounds(nx, y))
      continue;

    Cell &target = ctx.currentGrid.GetNext(nx, y);
    if (target.element == Element::WATER) {
      float sourceP = GetPressure(x, y, ctx.currentGrid);
      float targetP = GetPressure(nx, y, ctx.currentGrid);
      float diff = sourceP - targetP;
      target.pressure += diff * PRESSURE_EQ;
      source.pressure -= diff * PRESSURE_EQ;
    }
  }
}
