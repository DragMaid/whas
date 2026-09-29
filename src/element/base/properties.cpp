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
          .defaultHardness = 120.0f,
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
          .defaultTemperature = -10.0f,
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
          .defaultHardness = 250.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.5f, 0.5f, 0.02f},
          .density = 2500.0f,
          .restitution = 0.3f};
}

// Wood: static terrain that burns slowly and leaves smoke
constexpr ElementProperties MakeWood() {
  return {.mobile = false,
          .solid = true,
          .passable = false,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = true,
          .defaultTemperature = 20.0f,
          .defaultMass = 0.7f,
          .defaultHardness = 80.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.7f, 0.15f, 0.02f},
          .density = 700.0f,
          .restitution = 0.2f,
          .flammability = 0.08f,
          .ignitionTemp = 250.0f,
          .burnFuel = 4.0f,
          .burnTemp = 700.0f};
}

// Grass: a thin surface layer, walked through, burns fast
constexpr ElementProperties MakeGrass() {
  return {.mobile = false,
          .solid = true, // nothing sinks through it...
          .passable = true, // ...but characters and projectiles pass
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 20.0f,
          .defaultMass = 0.1f,
          .defaultHardness = 5.0f,
          .defaultLifetime = 0.0f,
          .lifetimeDecay = 0.0f,
          .defaultMoisture = 0.0f,
          .thermal = {1.0f, 0.3f, 0.02f},
          .density = 50.0f,
          .restitution = 0.0f,
          .flammability = 0.5f,
          .ignitionTemp = 150.0f,
          .burnFuel = 0.6f,
          .burnTemp = 600.0f};
}

// Smoke: rising gas left by burning, fades out
constexpr ElementProperties MakeSmoke() {
  return {.mobile = true,
          .solid = false,
          .passable = true,
          .rigidBody = false,
          .rigidBodyCandidate = false,
          .staticTerrain = false,
          .defaultTemperature = 60.0f,
          .defaultMass = 0.05f,
          .defaultHardness = 0.0f,
          .defaultLifetime = 3.0f,
          .lifetimeDecay = 0.016f,
          .defaultMoisture = 0.0f,
          .thermal = {1.0f, 0.05f, 0.2f},
          .density = 0.4f,
          .restitution = 0.0f};
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
  elements[static_cast<std::size_t>(Element::WOOD)] = MakeWood();
  elements[static_cast<std::size_t>(Element::GRASS)] = MakeGrass();
  elements[static_cast<std::size_t>(Element::SMOKE)] = MakeSmoke();
}
