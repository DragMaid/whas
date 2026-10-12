#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/fluid_movement_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"

namespace ElementsImpl {

namespace {

constexpr int DX4[] = {0, 0, -1, 1};
constexpr int DY4[] = {-1, 1, 0, 0};
constexpr uint32_t BITE_CHANCE = 90;    // 1 in this many ticks it bites
constexpr uint32_t DILUTE_CHANCE = 900; // ...or, beside water, thins out
constexpr float BOIL_POINT = 150.0f;

// What acid eats: soft ground and plants, and rock that isn't a body
bool Edible(const Cell &c) {
  switch (c.element) {
  case Element::EARTH:
  case Element::SAND:
  case Element::WOOD:
  case Element::GRASS:
  case Element::ICE:
  case Element::ROCK:
    return c.bodyID == -1;
  default:
    return false;
  }
}

} // namespace

// Acid flows like water and eats what it touches: each bite costs it some
// strength (its lifetime) and it's gone when that runs out. Water thins it
// back to water; heat boils it off as smoke.
void UpdateAcid(int x, int y, ElementContext &ctx) {
  Cell acid = ctx.grid.Get(x, y);
  if (acid.temperature >= BOIL_POINT) {
    MovementSystem::SetNext(x, y, ElementFactory::Create(Element::SMOKE, ctx.config), ctx);
    return;
  }
  if (ctx.rng.Below(BITE_CHANCE) == 0) {
    int i = static_cast<int>(ctx.rng.Below(4));
    int nx = x + DX4[i], ny = y + DY4[i];
    if (ctx.grid.InBounds(nx, ny)) {
      const Cell &n = ctx.grid.Get(nx, ny);
      if (Edible(n)) {
        // A wisp of smoke where it ate, now and then
        Element left = ctx.rng.Below(4) == 0 ? Element::SMOKE : Element::AIR;
        MovementSystem::SetNext(nx, ny, ElementFactory::Create(left, ctx.config), ctx);
        acid.lifetime -= 1.0f;
        if (acid.lifetime <= 0.0f) {
          MovementSystem::SetNext(x, y, ElementFactory::Create(Element::AIR, ctx.config),
                                  ctx);
          return;
        }
      } else if (n.element == Element::WATER &&
                 ctx.rng.Below(DILUTE_CHANCE / BITE_CHANCE) == 0) {
        MovementSystem::SetNext(x, y, ElementFactory::Create(Element::WATER, ctx.config),
                                ctx);
        return;
      }
    }
  }
  acid.pressure = PressureSystem::GetPressure(x, y, ctx.grid);
  FluidMovementSystem::UpdateLiquid(x, y, acid, ctx.config.fluid.water, ctx);
}

} // namespace ElementsImpl
