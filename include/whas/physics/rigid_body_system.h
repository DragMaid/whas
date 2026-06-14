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
  
  // Extract rigid bodies from the grid
  void ExtractBodies(Grid &grid, ElementContext &ctx);

private:
  b2WorldId m_worldId;
  
  struct BodyData {
      b2BodyId bodyId;
      std::vector<std::pair<float, float>> originalPixels;
      std::vector<Element> elements;
  };
  
  std::vector<BodyData> m_bodies;
};
