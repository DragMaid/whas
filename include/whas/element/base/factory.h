#pragma once
#include "whas/core/element.h"
#include "whas/core/cell.h"
#include "whas/core/config.h"

class ElementFactory {
public:
    static Cell Create(Element element, const SimulationConfig& config);
};
