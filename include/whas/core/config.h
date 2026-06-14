#pragma once
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
  int sparkChance = 4;
  float sparkTempScale = 0.6f;
  float sparkLifetimeScale = 0.5f;
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
  int meltChance = 10;
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
