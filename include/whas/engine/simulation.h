#pragma once
#include "whas/core/cell.h"
#include "whas/core/config.h"
#include "whas/core/element.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include <random>

class Simulation {
public:
  Simulation();

  void Update(float dt);

  void Paint(int cx, int cy, Element element, int brushRadius);
  void Erase(int cx, int cy, int brushRadius);

  const Cell &GetCell(int x, int y) const { return m_grid.Get(x, y); }

  int GetActiveChunks() const { return m_chunks.GetActiveChunksCount(); }
  int GetParticleCount() const { return m_particleCount; }
  float GetAvgPressure() const { return m_avgPressure; }
  float GetAvgTemp() const { return m_avgTemp; }

  // Runtime Tuning
  SimulationConfig &GetConfig() { return m_config; }

private:
  SimulationConfig m_config;      // Editable source
  SimulationConfig m_frameConfig; // Per-frame snapshot
  uint32_t m_frameCounter = 0;

  std::mt19937 m_rng;

  Grid m_grid;
  ChunkManager m_chunks;

  int m_particleCount = 0;
  float m_avgPressure = 0.0f;
  float m_avgTemp = 0.0f;

  void UpdateElements();
  void UpdatePhysics(float dt);
  void CollectStatistics();
};
