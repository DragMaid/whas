#include "whas/element/base/implementations.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"
#include <algorithm>

namespace ElementsImpl {
void UpdateCloud(int x, int y, ElementContext &ctx) {
  static constexpr float COOLING_RATE = 0.1f;
  static constexpr float TEMP_FREEZING_POINT = 0.0f;
  static constexpr float MIN_MOISTURE = 0.0f;

  // Wind & Drift
  static constexpr float WIND_JITTER_STRENGTH = 0.05f;
  static constexpr float MAX_DRIFT_SPEED = 0.5f;
  // Min speed required to attempt a step
  static constexpr float MOVE_THRESHOLD = 0.1f;

  // Rain mechanics
  // 1 in 120 frames under normal conditions
  static constexpr int CHANCE_TO_RAIN = 120;
  // 1 in 8 frames when cloud is collapsing
  static constexpr int CHANCE_TO_RAIN_BURST = 8;
  // How much moisture a raindrop costs the cloud
  static constexpr float RAIN_DROP_MOISTURE = 0.1f;
  static constexpr float NORMAL_RAIN_VELOCITY = 2.0f;
  static constexpr float BURST_RAIN_VELOCITY = 1.0f;

  Cell src = ctx.currentGrid.GetCurrent(x, y);

  //  Cloud Dissipation / Heavy Rain Burst Condensation
  // Triggered if the cloud gets too cold or runs completely out of water vapor
  if (src.temperature <= TEMP_FREEZING_POINT || src.moisture <= MIN_MOISTURE) {
    if (std::rand() % CHANCE_TO_RAIN_BURST == 0 &&
        src.moisture > MIN_MOISTURE) {
      int ry = y + 1;
      if (ctx.currentGrid.InBounds(x, ry) &&
          ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {

        Cell rain = ElementFactory::Create(Element::WATER);
        rain.velocityY = BURST_RAIN_VELOCITY;
        MovementSystem::SetNext(x, ry, rain, ctx);
      }
    }
    MovementSystem::SetNext(x, y, ElementFactory::Create(Element::AIR), ctx);
    return;
  }

  // Wind Jitter & Horizontal Drift Calculation
  float randomJitter =
      (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
  src.velocityX += randomJitter * WIND_JITTER_STRENGTH;
  src.velocityX = std::clamp(src.velocityX, -MAX_DRIFT_SPEED, MAX_DRIFT_SPEED);

  // Ambient Rain Generation (Normal operation)
  if (std::rand() % CHANCE_TO_RAIN == 0) {
    int ry = y + 1;
    if (ctx.currentGrid.InBounds(x, ry) &&
        ctx.currentGrid.GetCurrent(x, ry).element == Element::AIR) {

      Cell rain = ElementFactory::Create(Element::WATER);
      rain.velocityY = NORMAL_RAIN_VELOCITY;
      MovementSystem::SetNext(x, ry, rain, ctx);

      src.moisture -= RAIN_DROP_MOISTURE;
    }
  }

  // Execution of Drift Movement
  bool moved = false;
  if (std::abs(src.velocityX) >= MOVE_THRESHOLD) {
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
} // namespace Materials
