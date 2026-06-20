#include "whas/core/config.h"

// Air
constexpr ElementProperties MakeAir() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 20.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.0f, 0.05f, 0.1f},
          .density = 1.2f,
          .restitution = 0.0f};
}

// Water
constexpr ElementProperties MakeWater() {
  return {.mobile = true,
          .solid = false,
          .passable = false,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 15.0f,
          .defaultMass = 1.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 20.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {4.18f, 0.6f, 0.05f},
          .density = 1000.0f,
          .restitution = 0.1f};
}

// Earth
constexpr ElementProperties MakeEarth() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = true,
          .defaultTemperature = 20.0f,
          .defaultMass = 2.0f,
          .defaultHardness = 80.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.5f, 0.5016, 0.02f},
          .density = 2000.0f,
          .restitution = 0.2f};
}

// Fire
constexpr ElementProperties MakeFire() {
  return {.mobile = false,
          .solid = false,
          .passable = true,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 800.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 10.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {0.5f, 0.8f, 2.0f},
          .density = 0.5f,
          .restitution = 0.0f};
}

// Steam
constexpr ElementProperties MakeSteam() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 150.0f,
          .defaultMass = 0.1f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 8.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {2.0f, 0.2f, 0.05f},
          .density = 0.6f,
          .restitution = 0.0f};
}

// Cloud
constexpr ElementProperties MakeCloud() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 5.0f,
          .defaultMass = 0.0f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 20.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 1.0f,
          .thermal = {1.0f, 0.1f, 0.05f},
          .density = 0.3f,
          .restitution = 0.0f};
}

// Ice
constexpr ElementProperties MakeIce() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .rigidBody = true,
          .rigidBodyCandidate = true,
          .staticTerrain = false,
          .bodyMovable = true,
          .defaultTemperature = -100.0f,
          .defaultMass = 0.9f,
          .defaultHardness = 100.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {2.1f, 2.2f, 0.05f},
          .density = 917.0f,
          .restitution = 0.1f};
}

// Sand
constexpr ElementProperties MakeSand() {
  return {.mobile = true,
          .solid = true,
          .passable = false,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 25.0f,
          .defaultMass = 1.6f,
          .defaultHardness = 50.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {0.8f, 0.27f, 0.02f},
          .density = 1600.0f,
          .restitution = 0.05f};
}

// Rock
constexpr ElementProperties MakeRock() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .rigidBody = true,
          .rigidBodyCandidate = true,
          .staticTerrain = false,
          .bodyMovable = true,
          .defaultTemperature = 20.0f,
          .defaultMass = 3.0f,
          .defaultHardness = 95.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.5f, 0.5f, 0.02f},
          .density = 2500.0f,
          .restitution = 0.3f};
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
  elements[static_cast<std::size_t>(Element::ROCK)] = MakeRock();
}
