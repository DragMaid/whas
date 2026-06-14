#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/fluid_movement_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"

namespace ElementsImpl {

void UpdateWater(int x, int y, ElementContext &ctx) {
  Cell water = ctx.grid.Get(x, y);

  if (water.temperature >= WATER_BOILING_POINT) {
    Cell steam = ElementFactory::Create(Element::STEAM, ctx.config);
    steam.temperature = water.temperature;
    MovementSystem::SetNext(x, y, steam, ctx);
    return;
  }

  if (water.temperature <= WATER_FREEZING_POINT) {
    Cell ice = ElementFactory::Create(Element::ICE, ctx.config);
    ice.temperature = water.temperature;
    MovementSystem::SetNext(x, y, ice, ctx);
    return;
  }

  water.pressure = PressureSystem::GetPressure(x, y, ctx.grid);

  FluidMovementSystem::UpdateLiquid(x, y, water, ctx.config.fluid.water, ctx);
}

} // namespace ElementsImpl
