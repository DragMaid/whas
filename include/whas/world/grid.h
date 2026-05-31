#pragma once
#include "whas/core/cell.h"
#include <vector>

class Grid {
public:
  Grid();

  void Swap();
  void ClearNext();

  // Get current cell based on x, y position
  // Const overload variation provided
  Cell& GetCurrent(int x, int y);
  const Cell& GetCurrent(int x, int y) const;

  Cell& GetNext(int x, int y);
  const Cell& GetNext(int x, int y) const;

  bool InBounds(int x, int y) const;

  std::vector<Cell>& GetCurrentBuffer() { return m_current; }
  std::vector<Cell>& GetNextBuffer() { return m_next; }

private:
  std::vector<Cell> m_current;
  std::vector<Cell> m_next;
};
