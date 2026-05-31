#pragma once

#include "whas/element/base/econtext.h"

class ErosionSystem {
public:
  static bool TryErode(int wx, int wy, int ex, int ey, ElementContext &ctx);
};
