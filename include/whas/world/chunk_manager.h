#pragma once
#include "whas/world/chunk.h"
#include <vector>

class ChunkManager {
public:
  ChunkManager();

  void BeginFrame();
  void WakeChunkAt(int x, int y, uint32_t frameIndex, bool staticChange = false);
  void WakeNeighbourChunks(int cx, int cy, uint32_t frameIndex, bool staticChange = false);

  Chunk &GetChunk(int cx, int cy);
  const std::vector<Chunk> &GetChunks() const { return m_chunks; };
  std::vector<Chunk> &GetChunks() { return m_chunks; };
  int GetActiveChunksCount() const { return m_activeChunks; };

  void SetActiveCount(int cx, int cy, int count);

private:
  std::vector<Chunk> m_chunks;
  int m_activeChunks = 0;
};
