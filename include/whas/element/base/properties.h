#pragma once
#include "whas/core/element.h"
#include <array>
#include <cstddef>

struct ElementProperties {
  bool mobile;
  bool solid;
  bool passable;

  float density;
  float defaultTemperature;
  float defaultMass;
  float defaultHardness;

  float defaultLifetime = 0.0f;
  float lifetimeDecay = 0.0f;
  float defaultMoisture = 0.0f;
};

using PropertiesArray =
    std::array<ElementProperties, static_cast<size_t>(Element::COUNT)>;

class ElementRegistry {
public:
  static const ElementProperties &GetProperties(Element element);

private:
  static PropertiesArray s_properties;
};
