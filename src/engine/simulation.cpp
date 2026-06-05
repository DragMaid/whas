#include "whas/engine/simulation.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/registry.h"
#include "whas/physics/heat_system.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>

Simulation::Simulation()
    : m_config(), m_frameConfig(), m_rng(42), m_grid(m_config) {}

void Simulation::Update(float dt) {

  // TODO: disable all chunk activation later
  std::vector<Chunk> &allChunks = m_chunks.GetChunks();
  for (Chunk &c : allChunks)
    c.Wake();

  m_grid.ClearNext();
  m_chunks.BeginFrame();

  // Per-frame snapshotting for determinism and hot-reload safety
  m_frameConfig = m_config;

  ElementContext ctx{m_grid, m_chunks, m_rng, m_frameConfig};
  PressureSystem::Update(ctx);

  // TODO: if the element perform multitep then the the further
  // processing will hence be skipped
  UpdateElements();
  UpdatePhysics(dt);

  // Swap next state with current state
  m_grid.Swap();
  CollectStatistics();
}

void Simulation::UpdateElements() {
  ElementContext ctx{m_grid, m_chunks, m_rng, m_frameConfig};

  // Shuffling the chunk indicies
  std::vector<int> chunkOrder;
  const std::vector<Chunk> &allChunks = m_chunks.GetChunks();
  for (int i = 0; i < (int)allChunks.size(); ++i)
    if (allChunks[i].active)
      chunkOrder.push_back(i);
  std::shuffle(chunkOrder.begin(), chunkOrder.end(), m_rng);

  for (int i : chunkOrder) {
    int chunkCol = i % CHUNK_COLS;
    int chunkRow = i / CHUNK_COLS;

    int x0 = chunkCol * CHUNK_SIZE;
    int y0 = chunkRow * CHUNK_SIZE;

    int x1 = std::min(x0 + CHUNK_SIZE, GRID_W);
    int y1 = std::min(y0 + CHUNK_SIZE, GRID_H);

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

    int count = 0;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; ++x)
        if (m_grid.GetNext(x, y).element != Element::AIR)
          ++count;
    m_chunks.SetActiveCount(chunkCol, chunkRow, count);
  }

  for (int i = 0; i < GRID_W * GRID_H; ++i)
    if (!m_grid.GetNextBuffer()[i].updated)
      m_grid.GetNextBuffer()[i] = m_grid.GetCurrentBuffer()[i];
}

void Simulation::UpdatePhysics(float dt) {
  ElementContext ctx{m_grid, m_chunks, m_rng, m_frameConfig};
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &source = m_grid.GetCurrent(x, y);
      HeatSystem::Propagate(x, y, ctx, dt);
      PressureSystem::Propagate(x, y, ctx);
    }
  }
}

void Simulation::Paint(int cx, int cy, Element element, int brushRadius) {

  // TODO: set the debug mode to invoke the singular paint here
  bool debug = false;
  if (debug) {
    int x = cx;
    int y = cy;
    Cell c = ElementFactory::Create(element, m_config);
    m_grid.GetCurrent(x, y) = c;
    m_grid.GetNext(x, y) = c;
    m_chunks.WakeChunkAt(x, y);
    return;
  }

  for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
    for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
      if (dx * dx + dy * dy > brushRadius * brushRadius)
        continue;

      int x = cx + dx;
      int y = cy + dy;

      if (!m_grid.InBounds(x, y))
        continue;

      Cell c = ElementFactory::Create(element, m_config);
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
