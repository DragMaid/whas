#include "whas/world/chunk_manager.h"
#include "whas/constants.h"
#include <cassert>

ChunkManager::ChunkManager() : m_chunks(CHUNK_COLS * CHUNK_ROWS) {
  for (Chunk &c : m_chunks)
    c.Wake();
}

// Begin frame for each chunk
// and increase count for every active chunks
void ChunkManager::BeginFrame() {
  m_activeChunks = 0;
  for (Chunk &c : m_chunks) {
    c.BeginFrame();
    if (c.active)
      ++m_activeChunks;
  }
}

void ChunkManager::WakeChunkAt(int x, int y) {
  int cx = x / CHUNK_SIZE;
  int cy = y / CHUNK_SIZE;
  if (cx < 0 || cx >= CHUNK_COLS || cy < 0 || cy >= CHUNK_ROWS)
    return;

  GetChunk(cx, cy).Wake();
  WakeNeighbourChunks(cx, cy);
}

// -1, 0, 1 for both y axis and x axis
// making a total of 9 directions (8 around and 1 self)
// wake all of them together
void ChunkManager::WakeNeighbourChunks(int cx, int cy) {
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      int ny = cy + dy;

      if (nx >= 0 && nx < CHUNK_COLS && ny >= 0 && ny < CHUNK_ROWS)
        m_chunks[ny * CHUNK_COLS + nx].Wake();
    }
  }
}

Chunk &ChunkManager::GetChunk(int cx, int cy) {
  assert(cx >= 0 && cx < CHUNK_COLS && cy >= 0 && cy < CHUNK_ROWS);
  return m_chunks[cy * CHUNK_COLS + cx];
}

void ChunkManager::SetActiveCount(int cx, int cy, int count) {
  assert(cx >= 0 && cx < CHUNK_COLS && cy >= 0 && cy < CHUNK_ROWS);
  m_chunks[cy * CHUNK_COLS + cx].activeCount = count;
}
