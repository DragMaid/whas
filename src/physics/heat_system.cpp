#include "whas/physics/heat_system.h"
#include "whas/constants.h"

void HeatSystem::Propagate(int x, int y, ElementContext &ctx) {
  Cell &source = ctx.currentGrid.GetCurrent(x, y);

  const int dx[4] = {0, 0, -1, 1};
  const int dy[4] = {-1, 1, 0, 0};

  for (int i = 0; i < 4; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];

    if (!ctx.currentGrid.InBounds(nx, ny))
      continue;

    // TODO: problem with transferring heat back
    // TODO: I dont think temperature goes down eventually
    // TODO: water doesn't also cool down stuff
    Cell& target = ctx.currentGrid.GetNext(nx, ny);
    float diff = source.temperature - target.temperature;
    if (diff > 0.1f) { 
        float transfer = diff * HEAT_DIFFUSE;
        target.temperature += transfer;
        source.temperature -= transfer;
    }
  }
}
