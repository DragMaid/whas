#pragma once
#include "whas/core/cell.h"
#include "whas/core/element.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include <random>

class Simulation {
public:
  Simulation();

  void Update(float dt);
  //void Render();

  void Paint(int cx, int cy, Element element, int brushRadius);
  void Erase(int cx, int cy, int brushRadius);

  const Cell &GetCell(int x, int y) const { return m_grid.GetCurrent(x, y); }

  int GetActiveChunks() const { return m_chunks.GetActiveChunksCount(); }
  int GetParticleCount() const { return m_particleCount; }
  float GetAvgPressure() const { return m_avgPressure; }
  float GetAvgTemp() const { return m_avgTemp; }

  // TODO: add these 2 systems later
  // Player& GetPlayer() { return m_player; }
  // SpellSystem& GetSpellSystem() { return m_spellSystem; }

private:
  Grid m_grid;
  ChunkManager m_chunks;
  // Player m_player;
  // SpellSystem m_spellSystem;

  std::mt19937 m_rng;

  int m_particleCount = 0;
  float m_avgPressure = 0.0f;
  float m_avgTemp = 0.0f;

  void UpdateElements();
  void UpdatePhysics();
  //void UpdateGameplay(float dt);
  void CollectStatistics();
};
