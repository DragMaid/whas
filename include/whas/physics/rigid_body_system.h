#pragma once
#include "whas/world/grid.h"
#include <box2d/box2d.h>
#include <vector>
#include <cstdint>

struct ElementContext;

class RigidBodySystem {
public:
  RigidBodySystem();
  ~RigidBodySystem();

  void Update(Grid &grid, float dt);
  void DrawDebug();
  
  // Extract rigid bodies from the grid
  void ExtractBodies(Grid &grid, ElementContext &ctx);

private:
  void AddGreedyShapes(b2BodyId bodyId, const std::vector<std::pair<int, int>>& pixels, 
                      float centerX, float centerY, float density);

  b2WorldId m_worldId;
  
  struct BodyData {
      b2BodyId bodyId;
      std::vector<std::pair<float, float>> originalPixels;
      std::vector<Element> elements;
      
      // For hole-free rendering
      int minX, maxX, minY, maxY;
      std::vector<bool> pixelMask;
      std::vector<Element> localElements;
  };
  
  std::vector<BodyData> m_bodies;
  std::vector<b2BodyId> m_staticBodies;
};
