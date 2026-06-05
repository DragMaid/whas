#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/implementations.h"
#include "whas/physics/fluid_movement_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"

namespace ElementsImpl {

static const LiquidProperties WaterProperties = {
    .density = WATER_DENSITY,
    .viscosity = WATER_VISCOSITY,
    .maxFallSpeed = WATER_MAX_FALL_SPEED,
    .maxHorizontalSpeed = WATER_MAX_HORIZONTAL_SPEED,
    .spreadFactor = WATER_SPREAD_FACTOR,
    .friction = WATER_FRICTION,
    .canDisplaceGas = true,
    .canErodeTerrain = true};

void UpdateWater(int x, int y, ElementContext &ctx) {
  Cell water = ctx.currentGrid.GetCurrent(x, y);

  if (water.temperature >= WATER_BOILING_POINT) {
    Cell steam = ElementFactory::Create(Element::STEAM);
    steam.temperature = water.temperature;
    MovementSystem::SetNext(x, y, steam, ctx);
    return;
  }

  if (water.temperature <= WATER_FREEZING_POINT) {
    Cell ice = ElementFactory::Create(Element::ICE);
    ice.temperature = water.temperature;
    MovementSystem::SetNext(x, y, ice, ctx);
    return;
  }

  // Read pre-calculated pressure value (O(1))
  water.pressure = PressureSystem::GetPressure(x, y, ctx.currentGrid);

  // Handles gravity, velocity, multi-step movement, and spreading
  FluidMovementSystem::UpdateLiquid(x, y, water, WaterProperties, ctx);
}

} // namespace ElementsImpl
