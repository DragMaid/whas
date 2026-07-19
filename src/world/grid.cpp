#include "whas/world/grid.h"
#include "whas/constants.h"
#include "whas/element/base/factory.h"

Grid::Grid(const SimulationConfig& config)
    : m_cells(GRID_W * GRID_H, ElementFactory::Create(Element::AIR, config)),
      m_pressure(GRID_W * GRID_H, 0.0f) {}

Cell &Grid::Get(int x, int y) { return m_cells[y * GRID_W + x]; }
const Cell &Grid::Get(int x, int y) const {
  return m_cells[y * GRID_W + x];
}

bool Grid::InBounds(int x, int y) const {
  // NOTE: the damn tool bar is considered here too
  // TODO: remove this hard-coded part for the real panel setting
  return x >= 0 && x < GRID_W && y >= 0 && y < GRID_H;
}
