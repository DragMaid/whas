#pragma once
#include "raylib.h"
#include "whas/engine/simulation.h"
#include <vector>

class Renderer {
public:
  Renderer() = default;
  void DrawWorld(const Simulation &sim);
  void DrawDebugOverlay(const Simulation &sim);

private:
  Color CellColor(const Cell &c) const;

  // The grid is drawn as one texture (a pixel per cell, scaled up) rather
  // than a rectangle per cell. Left to the GL context to free at shutdown.
  std::vector<Color> m_pixels;
  Texture2D m_worldTexture{};
  Color BaseCellColor(const Cell &c) const;
};
