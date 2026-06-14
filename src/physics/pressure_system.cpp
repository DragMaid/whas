#include "whas/physics/pressure_system.h"
#include "whas/constants.h"

void PressureSystem::Update(ElementContext &ctx) {
  auto &pressureBuffer = ctx.grid.GetPressureBuffer();
  const auto &fConfig = ctx.config.fluid;

  for (int x = 0; x < GRID_W; ++x) {
    float currentPressure = 0.0f;
    for (int y = 0; y < GRID_H; ++y) {
      const Cell &cell = ctx.grid.Get(x, y);

      if (cell.element == Element::WATER) {
        pressureBuffer[y * GRID_W + x] = currentPressure;
        currentPressure += fConfig.pressureWeight;

        float maxPressure = fConfig.pressureScanDepth * fConfig.pressureWeight;
        if (currentPressure > maxPressure) {
          currentPressure = maxPressure;
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
  Cell &source = ctx.grid.Get(x, y);
  if (source.element != Element::WATER)
    return;

  const int dx[2] = {-1, 1};
  for (int direction : dx) {
    int nx = x + direction;
    if (!ctx.grid.InBounds(nx, y))
      continue;

    Cell &target = ctx.grid.Get(nx, y);
    if (target.element == Element::WATER) {
      float sourceP = GetPressure(x, y, ctx.grid);
      float targetP = GetPressure(nx, y, ctx.grid);
      float diff = sourceP - targetP;
      target.pressure += diff * ctx.config.world.pressureEq;
      source.pressure -= diff * ctx.config.world.pressureEq;
    }
  }
}
