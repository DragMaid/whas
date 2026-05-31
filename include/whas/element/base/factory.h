#pragma once
#include "whas/core/element.h"
#include "whas/core/cell.h"

class ElementFactory {
public:
    static Cell Create(Element element);
};
