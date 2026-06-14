#include "whas/core/config.h"

// Air
constexpr ElementProperties MakeAir() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .defaultTemperature = 20.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.0f, 0.05f, 0.1f},
          .density = 1.2f};
}

// Water
constexpr ElementProperties MakeWater() {
  return {.mobile = true,
          .solid = false,
          .passable = false,
          .defaultTemperature = 15.0f,
          .defaultMass = 1.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 20.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {4.18f, 0.6f, 0.05f},
          .density = 1000.0f};
}

// Earth
constexpr ElementProperties MakeEarth() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .defaultTemperature = 20.0f,
          .defaultMass = 2.0f,
          .defaultHardness = 80.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.5f, 0.5016, 0.02f},
          .density = 2000.0f};
}

// Fire
constexpr ElementProperties MakeFire() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .defaultTemperature = 800.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 10.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {0.5f, 0.8f, 2.0f},
          .density = 0.5f};
}

// Steam
constexpr ElementProperties MakeSteam() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .defaultTemperature = 150.0f,
          .defaultMass = 0.1f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 8.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {2.0f, 0.2f, 0.05f},
          .density = 0.6f};
}

// Cloud
constexpr ElementProperties MakeCloud() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .defaultTemperature = 5.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 20.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 1.0f,
          .thermal = {1.0f, 0.1f, 0.05f},
          .density = 0.3f};
}

// Ice
constexpr ElementProperties MakeIce() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .defaultTemperature = -5.0f,
          .defaultMass = 0.9f,
          .defaultHardness = 100.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {2.1f, 2.2f, 0.05f},
          .density = 917.0f};
}

// Sand
constexpr ElementProperties MakeSand() {
  return {.mobile = true,
          .solid = true,
          .passable = false,
          .defaultTemperature = 25.0f,
          .defaultMass = 1.6f,
          .defaultHardness = 50.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {0.8f, 0.27f, 0.02f},
          .density = 1600.0f};
}

SimulationConfig::SimulationConfig() {
  elements[static_cast<std::size_t>(Element::AIR)] = MakeAir();
  elements[static_cast<std::size_t>(Element::WATER)] = MakeWater();
  elements[static_cast<std::size_t>(Element::EARTH)] = MakeEarth();
  elements[static_cast<std::size_t>(Element::FIRE)] = MakeFire();
  elements[static_cast<std::size_t>(Element::STEAM)] = MakeSteam();
  elements[static_cast<std::size_t>(Element::CLOUD)] = MakeCloud();
  elements[static_cast<std::size_t>(Element::ICE)] = MakeIce();
  elements[static_cast<std::size_t>(Element::SAND)] = MakeSand();
}
