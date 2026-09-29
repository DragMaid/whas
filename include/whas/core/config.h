#pragma once
#include <algorithm>
#include "whas/element/base/properties.h"
#include "whas/physics/fluid_properties.h"

struct WorldConfig {
  float gravity = 1.0f;
  float pressureEq = 0.15f;
  float ambientTemp = 20.0f;
};

struct FluidConfig {
  int pressureScanDepth = 20;
  float pressureWeight = 0.5f;
  float gasDisplacementChance = 0.5f;

  // Default Water Properties
  LiquidProperties water = {
      1.0f, // density
      0.1f, // viscosity
      5.0f, // maxFallSpeed
      3.0f, // maxHorizontalSpeed
      0.1f, // spreadFactor
      0.7f, // friction
      true, // canDisplaceGas
      true  // canErodeTerrain
  };
};

struct CloudConfig {
  float freezingPoint = 0.0f;
  float minMoisture = 0.0f;
  float windJitter = 0.05f;
  float maxDrift = 0.5f;
  float moveThreshold = 0.1f;
  int rainChance = 120;
  float rainMoistureCost = 0.1f;
  float rainVelocity = 2.0f;
  float rainBurstVelocity = 1.0f;
};

struct FireConfig {
  float minTemp = 200.0f;
  float burnTemp = 800.0f;
  // hotter-than-default fire burns this much longer
  float maxFuelScale = 2.5f; 
  int sparkChance = 4;
  float sparkTempScale = 0.6f;
  float sparkLifetimeScale = 0.5f;

  // Heat a fire cell adds to each touching flammable, ice or water cell per tick
  float contactHeat = 25.0f;
  // Chance per tick a burning cell puts a flame in the air above it
  float flameChance = 0.35f;
  float smokeChance = 0.04f;
  // Chance per tick fire scorches a touching earth/rock/sand cell
  float charChance = 0.02f;
  // Temperature a burning cell drops to when water puts it out
  float extinguishTemp = 60.0f;
};

struct SteamConfig {
  float condensationTemp = 90.0f;
  float cloudFormationHeightRatio = 0.15f;
  float cloudFormationTemp = 95.0f;
  float cloudTempLoss = 50.0f;
  float buoyancyBase = -1.5f;
  float buoyancyTempScale = 0.05f;
  float driftStrength = 0.1f;
  float maxDrift = 1.0f;
};

struct EarthConfig {
  float fireHardnessLoss = 0.5f;
};

struct IceConfig {
  float meltPoint = 0.0f;
  int meltChance = 3; // 1 in this many ticks, once above the melt point
};

struct SimulationConfig {
  WorldConfig world;
  FluidConfig fluid;
  CloudConfig cloud;
  FireConfig fire;
  SteamConfig steam;
  EarthConfig earth;
  IceConfig ice;

  std::array<ElementProperties, static_cast<std::size_t>(Element::COUNT)>
      elements;

  // Constructor defined in property registration
  SimulationConfig();
};

// Mass of one cell of an element for pushes and impacts. Massless elements
// (fire, cloud) still get a small mass so forces stay finite.
inline float ElementMass(const SimulationConfig &config, Element element) {
  return std::max(0.05f,
                  config.elements[static_cast<std::size_t>(element)].defaultMass);
}
