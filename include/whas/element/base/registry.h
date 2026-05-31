#pragma once
#include "whas/core/element.h"
#include "whas/element/base/econtext.h"
#include <array>

using ElementUpdateFn = void (*)(int x, int y, ElementContext &ctx);
using ElementUpdateArray =
    std::array<ElementUpdateFn, static_cast<size_t>(Element::COUNT)>;

class ElementUpdateRegistry {
public:
  static void Update(Element element, int x, int y, ElementContext &ctx);

private:
  static ElementUpdateArray s_updateFunctions;
};
