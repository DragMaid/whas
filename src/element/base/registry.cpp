#include "whas/element/base/registry.h"
#include "whas/element/base/implementations.h"

ElementUpdateArray ElementUpdateRegistry::s_updateFunctions = {
    nullptr, // Air has no update available
    ElementsImpl::UpdateWater,
    ElementsImpl::UpdateEarth,
    ElementsImpl::UpdateFire,
    ElementsImpl::UpdateSteam,
    ElementsImpl::UpdateCloud,
    ElementsImpl::UpdateIce,
};

void ElementUpdateRegistry::Update(Element element, int x, int y,
                                   ElementContext &ctx) {
  auto fn = s_updateFunctions[static_cast<size_t>(element)];
  if (fn)
    fn(x, y, ctx);
}
