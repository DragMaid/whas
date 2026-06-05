#pragma once

#include "whas/core/cell.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/fluid_properties.h"

namespace FluidMovementSystem {
    void UpdateLiquid(
        int x,
        int y,
        Cell& cell,
        const LiquidProperties& properties,
        ElementContext& ctx);
}
