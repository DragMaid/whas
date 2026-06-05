#pragma once

#include "whas/element/base/econtext.h"

class PressureSystem {
public:
  static void Update(ElementContext &ctx);
  static float GetPressure(int x, int y, Grid &grid);
  static void Propagate(int x, int y, ElementContext &ctx);
};

