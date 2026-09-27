#pragma once
#include "raylib.h"
#include "whas/engine/simulation.h"

class Renderer {
public:
  Renderer() = default;
  void DrawWorld(const Simulation &sim);
  void DrawDebugOverlay(const Simulation &sim);

private:
  Color CellColor(const Cell &c) const;
  Color BaseCellColor(const Cell &c) const;
};
