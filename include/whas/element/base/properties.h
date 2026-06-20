#pragma once
#include "whas/core/element.h"
#include <array>
#include <cstddef>
#include <cstdint>

struct ThermalProperties {
  float heatCapacity = 1.0f;  // how much energy required to heat
  float conductivity = 0.1f;  // how fast heat spreads
  float coolingRate = 0.05f;  // heat lost to ambient
};

struct ElementProperties {
  bool mobile;
  bool solid;
  bool passable;
  // Whether this element can be grouped into a rigid body blob
  bool rigidBody; // legacy: kept for compatibility
  // New explicit flags (see refactor notes):
  // Whether this element is eligible to be extracted into a Box2D rigid body
  bool rigidBodyCandidate;
  // Whether this element contributes to permanent static terrain meshes
  bool staticTerrain;
  // If true, rigid bodies formed from this element are dynamic/movable in Box2D.
  // If false, extracted bodies will be created as static terrain bodies.
  bool bodyMovable;

  // Collision layer mask for Box2D filtering and interactions
  uint32_t collisionLayer = 1;

  // Cellular automata update priority (lower runs earlier)
  uint8_t caPriority = 0;

  float defaultTemperature;
  float defaultMass;
  float defaultHardness;

  float defaultLifetime = 0.0f;
  float lifetimeDecay = 0.0f;
  float defaultMoisture = 0.0f;

  ThermalProperties thermal;

  // Physical traits
  float density = 0.0f;
  float restitution = 0.0f;
  float initialVx = 0.0f;
  float initialVy = 0.0f;
};

using PropertiesArray =
    std::array<ElementProperties, static_cast<size_t>(Element::COUNT)>;
