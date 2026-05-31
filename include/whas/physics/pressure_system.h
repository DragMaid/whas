#pragma once
#include "whas/element/base/econtext.h"

class PressureSystem {
public:
  static void Propagate(int x, int y, ElementContext &ctx);
};
