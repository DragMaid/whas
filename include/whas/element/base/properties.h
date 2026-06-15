#pragma once
#include "whas/core/element.h"
#include <array>
#include <cstddef>

struct ThermalProperties {
  float heatCapacity = 1.0f;  // how much energy required to heat
  float conductivity = 0.1f;  // how fast heat spreads
  float coolingRate = 0.05f;  // heat lost to ambient
};

struct ElementProperties {
  bool mobile;
  bool solid;
  bool passable;
  bool rigidBody;

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
