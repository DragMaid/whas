#include "whas/physics/heat_system.h"
#include "whas/core/config.h"
#include <algorithm>

void HeatSystem::Propagate(int x, int y, ElementContext &ctx, float dt) {
  Cell &source = ctx.currentGrid.GetNext(x, y);
  const ElementProperties &srcProps =
      ctx.config.elements[static_cast<size_t>(source.element)];

  float netHeatFlux = 0.0f;

  const int dx[4] = {0, 0, -1, 1};
  const int dy[4] = {-1, 1, 0, 0};

  for (int i = 0; i < 4; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];

    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;

    const Cell &target = ctx.currentGrid.GetNext(nx, ny);
    const ElementProperties &dstProps =
        ctx.config.elements[static_cast<size_t>(target.element)];

    float diff = source.temperature - target.temperature;
    float conductivity =
        (srcProps.thermal.conductivity + dstProps.thermal.conductivity) * 0.5f;
    netHeatFlux -= diff * conductivity;
  }

  // Update temperature based on heat capacity
  // We use a small factor to keep simulation stable
  source.temperature += (netHeatFlux * dt / srcProps.thermal.heatCapacity);

  // Apply cooling rate (return to default temp)
  float defaultTemp = srcProps.defaultTemperature;
  float coolingRate = srcProps.thermal.coolingRate;

  if (source.temperature > defaultTemp) {
    source.temperature =
        std::max(source.temperature - coolingRate, defaultTemp);
  } else if (source.temperature < defaultTemp) {
    source.temperature =
        std::min(source.temperature + coolingRate, defaultTemp);
  }
}
