#pragma once
#include "whas/core/cell.h"
#include "whas/core/config.h"
#include <vector>

class Grid {
public:
  Grid(const SimulationConfig& config);

  // Get cell based on x, y position
  Cell& Get(int x, int y);
  const Cell& Get(int x, int y) const;

  // Legacy accessors (mapping to Get for compatibility during migration)
  Cell& GetCurrent(int x, int y) { return Get(x, y); }
  const Cell& GetCurrent(int x, int y) const { return Get(x, y); }

  bool InBounds(int x, int y) const;

  std::vector<Cell>& GetBuffer() { return m_cells; }
  std::vector<float>& GetPressureBuffer() { return m_pressure; }

private:
  std::vector<Cell> m_cells;
  std::vector<float> m_pressure;
};
