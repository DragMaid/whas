#pragma once
#include "whas/core/config.h"
#include "whas/element/base/econtext.h"
#include "whas/world/grid.h"
#include <box2d/box2d.h>
#include <raylib.h>
#include <cstdint>
#include <vector>

struct ChunkMesh {
  b2BodyId bodyId;
  uint32_t lastChangeFrame;
  bool active = false;
};

struct BodyData {
  b2BodyId bodyId;
  std::vector<std::pair<float, float>> originalPixels;
  std::vector<Element> elements;

  // For hole-free rendering
  int minX, maxX, minY, maxY;
  std::vector<uint8_t> pixelMask;
  std::vector<Element> localElements;
  // Each pixel's temperature, carried while the body moves (the grid cells
  // are cleared and rewritten every frame)
  std::vector<float> localTemperatures;

  bool shouldBreak = false;
};

class RigidBodySystem {
public:
  RigidBodySystem();
  ~RigidBodySystem();

  // New Noita-style update flow
  void PreUpdate(Grid &grid, ElementContext &ctx);
  void PostUpdate(Grid &grid, ElementContext &ctx,
                  class ParticleSystem &particles, SimulationConfig &config,
                  float dt);

  void DrawDebug();

  // Drop every body and start an empty world (loading a snapshot). The grid
  // must not reference bodies any more; terrain meshes rebuild on the next
  // update and loose chunks are extracted again from the cells.
  void Reset();

  // Hash of every dynamic body's transform and velocity (lockstep checks)
  uint64_t StateHash() const;

  // Push the body that owns a grid cell (cell.bodyID), impulse and point in
  // cell units. Unknown ids are ignored.
  void ApplyImpulse(int32_t cellBodyID, Vector2 impulse, Vector2 point);

  // Extract rigid bodies from the grid (for initial or new dynamic bodies)
  void ExtractDynamicBodies(Grid &grid, ElementContext &ctx,
                            class ParticleSystem &particles);

private:
  void CreateWorld();
  void UpdateWorldMeshes(Grid &grid, ElementContext &ctx);
  void ProcessDisplacement(Grid &grid, ElementContext &ctx,
                           class ParticleSystem &particles);
  void SyncBackToGrid(Grid &grid, ElementContext &ctx);
  // Report bodies striking something hard enough to hear
  void HearHits(class ParticleSystem &particles);

  void AddTriangulatedShapes(b2BodyId bodyId, const std::vector<bool> &mask,
                             int width, int height, float centerX,
                             float centerY, float density, float restitution);
  void ClearBodiesFromGrid(Grid &grid, SimulationConfig &config);

  b2WorldId m_worldId;

  std::vector<BodyData> m_bodies;
  std::vector<ChunkMesh> m_chunkMeshes;
};
