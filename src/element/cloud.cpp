#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {

struct CloudProps {
  float coolingRate = 0.1f;
  float freezingPoint = 0.0f;
  float minMoisture = 0.0f;
  float windJitter = 0.05f;
  float maxDrift = 0.5f;
  float moveThreshold = 0.1f;
  int rainChance = 120;
  int rainBurstChance = 8;
  float rainMoistureCost = 0.1f;
  float rainVelocity = 2.0f;
  float rainBurstVelocity = 1.0f;
};

static const CloudProps LocalCloudProps;

void UpdateCloud(int x, int y, ElementContext &ctx) {
  Cell src = ctx.currentGrid.GetCurrent(x, y);

  // 1. Cloud Dissipation / Heavy Rain Burst
  // Triggered if the cloud gets too cold or runs completely out of moisture
  if (src.temperature <= LocalCloudProps.freezingPoint ||
      src.moisture <= LocalCloudProps.minMoisture) {
    if (std::rand() % LocalCloudProps.rainBurstChance == 0 &&
        src.moisture > LocalCloudProps.minMoisture) {
      int ry = y + 1;
      if (ctx.currentGrid.InBounds(x, ry) &&
          ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {
        Cell rain = ElementFactory::Create(Element::WATER);
        rain.velocityY = LocalCloudProps.rainBurstVelocity;
        MovementSystem::SetNext(x, ry, rain, ctx);
      }
    }
    // Dissipate into air
    MovementSystem::SetNext(x, y, ElementFactory::Create(Element::AIR), ctx);
    return;
  }

  // 2. Wind Jitter & Horizontal Drift
  float randomJitter =
      (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f -
      1.0f;
  src.velocityX += randomJitter * LocalCloudProps.windJitter;
  src.velocityX = std::clamp(src.velocityX, -LocalCloudProps.maxDrift,
                             LocalCloudProps.maxDrift);

  // 3. Ambient Rain Generation
  if (std::rand() % LocalCloudProps.rainChance == 0) {
    int ry = y + 1;
    if (ctx.currentGrid.InBounds(x, ry) &&
        ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {
      Cell rain = ElementFactory::Create(Element::WATER);
      rain.velocityY = LocalCloudProps.rainVelocity;
      MovementSystem::SetNext(x, ry, rain, ctx);

      // Consume moisture for every raindrop
      src.moisture -= LocalCloudProps.rainMoistureCost;
    }
  }

  // 4. Execution of Drift Movement
  bool moved = false;
  if (std::abs(src.velocityX) >= LocalCloudProps.moveThreshold) {
    int dir = (src.velocityX > 0.0f) ? 1 : -1;
    int tx = x + dir;

    if (ctx.currentGrid.InBounds(tx, y) &&
        ctx.currentGrid.GetCurrent(tx, y).element == Element::AIR) {
      moved = MovementSystem::TryMove(x, y, tx, y, ctx);
    }
  }

  if (!moved) {
    MovementSystem::SetNext(x, y, src, ctx);
  }
}
} // namespace ElementsImpl
