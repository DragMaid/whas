#include "whas/engine/simulation.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/registry.h"
#include "whas/physics/heat_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>

Simulation::Simulation() : m_rng(42) {}

void Simulation::Update(float dt) {
  m_grid.ClearNext();
  m_chunks.BeginFrame();

  UpdateElements();
  UpdatePhysics();
  // UpdateGameplay(dt);

  // Swap next state with current state
  m_grid.Swap();
  CollectStatistics();
}

void Simulation::UpdateElements() {
  ElementContext ctx{m_grid, m_chunks, m_rng};

  // Shuffling the chunk indicies
  // The idea is to make sure that the chunk updating
  // doesn't become biased, which can lead to patterns
  // arising (Example: faster updates on left)
  std::vector<int> chunkOrder;
  const std::vector<Chunk> &allChunks = m_chunks.GetChunks();
  for (int i = 0; i < (int)allChunks.size(); ++i)
    if (allChunks[i].active)
      chunkOrder.push_back(i);
  std::shuffle(chunkOrder.begin(), chunkOrder.end(), m_rng);

  for (int i : chunkOrder) {
    // Convert indicies to column and row
    int chunkCol = i % CHUNK_COLS;
    int chunkRow = i / CHUNK_ROWS;

    int x0 = chunkCol * CHUNK_SIZE;
    int y0 = chunkRow * CHUNK_SIZE;

    // Make sure the chunk from this point do not go out of bound
    int x1 = std::min(x0 + CHUNK_SIZE, GRID_W);
    int y1 = std::min(y0 + CHUNK_SIZE, GRID_H);

    // NOTE: push_backs create a new copy of the vector with
    // the new element added to the end while emplace_back
    // insert it directly to current vecotr (faster)
    std::vector<std::pair<int, int>> cells;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        cells.emplace_back(x, y);
    std::shuffle(cells.begin(), cells.end(), m_rng);

    for (const auto &[x, y] : cells) {
      if (m_grid.GetNext(x, y).updated)
        continue;

      const Cell &source = m_grid.GetCurrent(x, y);
      if (source.element == Element::AIR)
        MovementSystem::Carry(x, y, ctx);
      else
        ElementUpdateRegistry::Update(source.element, x, y, ctx);
    }

    // Set the number of active cells in the chunk
    int count = 0;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; ++x)
        if (m_grid.GetNext(x, y).element != Element::AIR)
          ++count;
    m_chunks.SetActiveCount(chunkCol, chunkRow, count);
  }

  // Check for all other cells that haven't been updated
  // and carry the last state over
  for (int i = 0; i < GRID_W * GRID_H; ++i)
    if (!m_grid.GetCurrentBuffer()[i].updated)
      m_grid.GetNextBuffer()[i] = m_grid.GetCurrentBuffer()[i];
}

void Simulation::UpdatePhysics() {
  ElementContext ctx{m_grid, m_chunks, m_rng};
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &source = m_grid.GetCurrent(x, y);
      if (source.element != Element::AIR) {
        HeatSystem::Propagate(x, y, ctx);
        PressureSystem::Propagate(x, y, ctx);
      }
    }
  }
}

void Simulation::Paint(int cx, int cy, Element element, int brushRadius) {
  for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
    for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
      if (dx * dx + dy * dy > brushRadius * brushRadius)
        continue;

      int x = cx + dx;
      int y = cy + dy;

      if (!m_grid.InBounds(x, y))
        continue;

      Cell c = ElementFactory::Create(element);
      m_grid.GetCurrent(x, y) = c;
      m_grid.GetNext(x, y) = c;
      m_chunks.WakeChunkAt(x, y);
    }
  }
}

void Simulation::Erase(int cx, int cy, int brushRadius) {
  Paint(cx, cy, Element::AIR, brushRadius);
}

void Simulation::CollectStatistics() {
  m_particleCount = 0;
  double pSum = 0.0, tSum = 0.0;
  for (const auto &c : m_grid.GetCurrentBuffer()) {
    if (c.element != Element::AIR) {
      ++m_particleCount;
      pSum += c.pressure;
      tSum += c.temperature;
    }
  }
  if (m_particleCount > 0) {
    m_avgPressure = static_cast<float>(pSum / m_particleCount);
    m_avgTemp = static_cast<float>(tSum / m_particleCount);
  }
}
