#include "whas/element/base/properties.h"

// Air
constexpr ElementProperties MakeAir() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .density = 1.2f,
          .defaultTemperature = 20.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f};
}

// Water
constexpr ElementProperties MakeWater() {
  return {.mobile = true,
          .solid = false,
          .passable = false,
          .density = 1000.0f,
          .defaultTemperature = 15.0f,
          .defaultMass = 1.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f};
}

// Earth
constexpr ElementProperties MakeEarth() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .density = 2000.0f,
          .defaultTemperature = 20.0f,
          .defaultMass = 2.0f,
          .defaultHardness = 80.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f};
}

// Fire
constexpr ElementProperties MakeFire() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .density = 0.5f,
          .defaultTemperature = 800.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 3.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f};
}

// Steam
constexpr ElementProperties MakeSteam() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .density = 0.6f,
          .defaultTemperature = 105.0f,
          .defaultMass = 0.1f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 8.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f};
}

// Cloud
constexpr ElementProperties MakeCloud() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .density = 0.3f,
          .defaultTemperature = 5.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 1.0f};
}

// Ice
constexpr ElementProperties MakeIce() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .density = 917.0f,
          .defaultTemperature = -5.0f,
          .defaultMass = 0.9f,
          .defaultHardness = 100.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f};
}

// NOTE: the registry need same ordering as the Enum
PropertiesArray ElementRegistry::s_properties = {
    MakeAir(),   MakeWater(), MakeEarth(), MakeFire(),
    MakeSteam(), MakeCloud(), MakeIce()};

const ElementProperties &ElementRegistry::GetProperties(Element element) {
  return s_properties[static_cast<size_t>(element)];
}
