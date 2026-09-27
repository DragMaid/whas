#pragma once
#include "whas/core/config.h"
#include "whas/core/det_rng.h"
#include "whas/spell/spell_system.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include <vector>

struct ElementContext {
  Grid &grid;
  ChunkManager &chunks;
  DetRng &rng;
  const SimulationConfig &config;
  uint32_t frameIndex;
  class ParticleSystem &particles;
  // Set on worker threads: particles spawned there are queued and added in
  // chunk order after the pass, so the pool doesn't depend on thread timing
  std::vector<struct PendingSpawn> *deferredSpawns = nullptr;
};
