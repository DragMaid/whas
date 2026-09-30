#include "whas/element/base/registry.h"
#include "whas/element/base/implementations.h"
#include "whas/element/base/factory.h"
#include "whas/physics/movement_system.h"

ElementUpdateArray ElementUpdateRegistry::s_updateFunctions = {
    nullptr, // Air has no update available
    ElementsImpl::UpdateWater,
    ElementsImpl::UpdateEarth,
    ElementsImpl::UpdateFire,
    ElementsImpl::UpdateSteam,
    ElementsImpl::UpdateCloud,
    ElementsImpl::UpdateIce,
    ElementsImpl::UpdateSand,
    nullptr, // Rock
    ElementsImpl::UpdateWood,
    ElementsImpl::UpdateGrass,
    ElementsImpl::UpdateSmoke,
    nullptr, // Light is never a cell
};

void ElementUpdateRegistry::Update(Element element, int x, int y,
                                   ElementContext &ctx) {
  Cell &cell = ctx.grid.Get(x, y);
  const auto &props = ctx.config.elements[static_cast<size_t>(element)];

  // Standardized Lifetime System
  if (cell.bodyID == -1 && props.lifetimeDecay > 0.0f && cell.lifetime >= 0.0f) {
    cell.lifetime -= props.lifetimeDecay;
    if (cell.lifetime <= 0.0f) {
      Cell air = ElementFactory::Create(Element::AIR, ctx.config);
      // TODO: move this to config also
      air.temperature = cell.temperature * 0.5f;
      MovementSystem::SetNext(x, y, air, ctx);
      return;
    }
  }

  auto fn = s_updateFunctions[static_cast<size_t>(element)];
  if (fn)
    fn(x, y, ctx);
}
