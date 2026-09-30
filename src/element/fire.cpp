#include "whas/element/base/implementations.h"
#include <algorithm>
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"

namespace ElementsImpl {

void UpdateFire(int x, int y, ElementContext &ctx) {
  const auto& fConfig = ctx.config.fire;
  const auto &props = ctx.config.elements[static_cast<size_t>(Element::FIRE)];
  Cell src = ctx.grid.Get(x, y);

  // Combustion: while there's fuel left the fire makes its own heat
  if (props.defaultLifetime > 0.0f && src.lifetime > 0.0f) {
    float fuel = std::min(src.lifetime / props.defaultLifetime,
                          fConfig.maxFuelScale);
    src.temperature = std::max(
        src.temperature,
        fConfig.minTemp + (fConfig.burnTemp - fConfig.minTemp) * std::min(fuel, 1.0f));
  }

  // If the fire gets too cold, it dies and turns into air
  if (src.temperature < fConfig.minTemp) {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    MovementSystem::SetNext(x, y, air, ctx);
    return;
  }

  // Heat what the flame touches: flammables catch, ice melts, water boils.
  // Water fights back and puts the flame out.
  static constexpr int DX4[] = {0, 0, -1, 1};
  static constexpr int DY4[] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; ++i) {
    int nx = x + DX4[i], ny = y + DY4[i];
    if (!ctx.grid.InBounds(nx, ny))
      continue;
    Cell &n = ctx.grid.Get(nx, ny);
    const auto &nProps = ctx.config.elements[static_cast<size_t>(n.element)];
    if (n.element == Element::WATER) {
      n.temperature += fConfig.contactHeat;
      Cell steam = ElementFactory::Create(Element::STEAM, ctx.config);
      MovementSystem::SetNext(x, y, steam, ctx);
      return;
    }
    if (nProps.flammability > 0.0f || n.element == Element::ICE)
      n.temperature += fConfig.contactHeat;
    else if ((n.element == Element::EARTH || n.element == Element::ROCK ||
              n.element == Element::SAND) &&
             ctx.rng.Unit() < fConfig.charChance)
      n.flags |= CELL_CHARRED;
  }

  if (ctx.rng.Unit() < fConfig.smokeChance * 0.25f) {
    int uy = y - 1;
    if (ctx.grid.InBounds(x, uy) &&
        ctx.grid.Get(x, uy).element == Element::AIR)
      MovementSystem::SetNext(
          x, uy, ElementFactory::Create(Element::SMOKE, ctx.config), ctx);
  }

  // Spontaneous Sparking
  if (ctx.rng.Below(fConfig.sparkChance) == 0) {
    int uy = y - 1;
    if (ctx.grid.InBounds(x, uy)) {
      Cell spark = ElementFactory::Create(Element::FIRE, ctx.config);
      if (MovementSystem::CanDisplace(spark, ctx.grid.Get(x, uy), ctx)) {
        spark.temperature = src.temperature * fConfig.sparkTempScale;
        spark.lifetime = src.lifetime * fConfig.sparkLifetimeScale;
        MovementSystem::SetNext(x, uy, spark, ctx);
      }
    }
  }

  MovementSystem::SetNext(x, y, src, ctx);
}
} // namespace ElementsImpl
