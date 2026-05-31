#include "whas/world/grid.h"
#include "whas/constants.h"
#include "whas/element/base/factory.h"

// Constructor
// Fill the entire grid with air on init
Grid::Grid()
    : m_current(GRID_W * GRID_H, ElementFactory::Create(Element::AIR)),
      m_next(GRID_W * GRID_H, ElementFactory::Create(Element::AIR)) {}

void Grid::Swap() { std::swap(m_current, m_next); }

void Grid::ClearNext() {
  for (Cell &c : m_next)
    c.updated = false;
}

// This help access the 2D grid flattened into a 1D vector
Cell &Grid::GetCurrent(int x, int y) { return m_current[y * GRID_W + x]; }
const Cell &Grid::GetCurrent(int x, int y) const {
  return m_current[y * GRID_W + x];
}

Cell &Grid::GetNext(int x, int y) { return m_current[y * GRID_W + x]; }
const Cell &Grid::GetNext(int x, int y) const {
  return m_current[y * GRID_W + x];
}

bool Grid::InBounds(int x, int y) const {
  return x >= 0 && x < GRID_W && y >= 0 && y < GRID_H;
}
