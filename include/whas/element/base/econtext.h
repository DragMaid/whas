#pragma once
#include "whas/core/config.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include <random>

struct ElementContext {
  Grid &grid;
  ChunkManager &chunks;
  std::mt19937 &rng;
  const SimulationConfig &config;
  uint32_t frameIndex;
};
