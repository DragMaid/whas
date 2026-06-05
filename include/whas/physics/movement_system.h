#pragma once

#include "whas/element/base/econtext.h"

class MovementSystem {
public:
  static bool TryMove(int x, int y, int tx, int ty, Cell &c, ElementContext &ctx);
  static void SetNext(int x, int y, const Cell &c, ElementContext &ctx);
  static void Carry(int x, int y, ElementContext &ctx);
};
