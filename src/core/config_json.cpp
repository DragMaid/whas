#include "whas/core/config_json.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

using nlohmann::json;
using Kind = ConfigField::Kind;

namespace {

// Element groups need a name that outlives the visit
const char *ElementGroup(size_t i) {
  static const auto names = [] {
    std::array<std::string, static_cast<size_t>(Element::COUNT)> n;
    for (size_t e = 0; e < n.size(); ++e)
      n[e] = std::string("Element/") + ElementName(static_cast<Element>(e));
    return n;
  }();
  return names[i].c_str();
}

} // namespace

void ForEachConfigField(
    SimulationConfig &c,
    const std::function<void(const ConfigField &, void *)> &visit) {
  auto f = [&](const char *group, const char *key, const char *label,
               float &v, float lo, float hi) {
    visit({group, key, label, Kind::Float, lo, hi}, &v);
  };
  auto i = [&](const char *group, const char *key, const char *label, int &v,
               int lo, int hi) {
    visit({group, key, label, Kind::Int, float(lo), float(hi)}, &v);
  };
  auto b = [&](const char *group, const char *key, const char *label,
               bool &v) { visit({group, key, label, Kind::Bool}, &v); };

  f("World", "gravity", "Gravity", c.world.gravity, -1.0f, 2.0f);
  f("World", "pressureEq", "Pressure balance", c.world.pressureEq, 0.0f, 1.0f);
  f("World", "ambientTemp", "Ambient temperature", c.world.ambientTemp, -50.0f,
    100.0f);

  b("Weather", "infiniteRain", "Endless rain from clouds",
    c.cloud.infiniteRain);
  f("Weather", "skyRain", "Rain from the sky", c.cloud.skyRain, 0.0f, 8.0f);
  i("Weather", "rainChance", "Cloud rain chance (1 in)", c.cloud.rainChance,
    1, 500);
  f("Weather", "rainMoistureCost", "Moisture per drop",
    c.cloud.rainMoistureCost, 0.0f, 1.0f);
  f("Weather", "rainVelocity", "Raindrop speed", c.cloud.rainVelocity, 0.0f,
    10.0f);
  f("Weather", "freezingPoint", "Cloud freezing point", c.cloud.freezingPoint,
    -20.0f, 20.0f);
  f("Weather", "windJitter", "Cloud wind jitter", c.cloud.windJitter, 0.0f,
    0.5f);
  f("Weather", "maxDrift", "Cloud drift", c.cloud.maxDrift, 0.0f, 5.0f);

  i("Fluid", "pressureScanDepth", "Pressure scan depth",
    c.fluid.pressureScanDepth, 1, 50);
  f("Fluid", "pressureWeight", "Pressure weight", c.fluid.pressureWeight, 0.0f,
    2.0f);
  f("Fluid", "gasDisplacementChance", "Gas displacement",
    c.fluid.gasDisplacementChance, 0.0f, 1.0f);
  f("Water", "density", "Density", c.fluid.water.density, 0.1f, 10.0f);
  f("Water", "viscosity", "Viscosity", c.fluid.water.viscosity, 0.0f, 1.0f);
  f("Water", "maxFallSpeed", "Fall speed", c.fluid.water.maxFallSpeed, 0.0f,
    20.0f);
  f("Water", "maxHorizontalSpeed", "Flow speed",
    c.fluid.water.maxHorizontalSpeed, 0.0f, 20.0f);
  f("Water", "spreadFactor", "Spread", c.fluid.water.spreadFactor, 0.0f, 1.0f);
  f("Water", "friction", "Friction", c.fluid.water.friction, 0.0f, 1.0f);
  b("Water", "canErodeTerrain", "Erodes terrain",
    c.fluid.water.canErodeTerrain);

  f("Fire", "minTemp", "Minimum temperature", c.fire.minTemp, 0.0f, 1000.0f);
  f("Fire", "burnTemp", "Burn temperature", c.fire.burnTemp, 100.0f, 2000.0f);
  f("Fire", "maxFuelScale", "Fuel scale", c.fire.maxFuelScale, 0.5f, 5.0f);
  i("Fire", "sparkChance", "Spark chance (1 in)", c.fire.sparkChance, 1, 50);
  f("Fire", "contactHeat", "Contact heat", c.fire.contactHeat, 0.0f, 100.0f);
  f("Fire", "flameChance", "Flame chance", c.fire.flameChance, 0.0f, 1.0f);
  f("Fire", "smokeChance", "Smoke chance", c.fire.smokeChance, 0.0f, 0.5f);
  f("Fire", "charChance", "Scorch chance", c.fire.charChance, 0.0f, 0.2f);

  f("Steam", "condensationTemp", "Condensation temperature",
    c.steam.condensationTemp, 0.0f, 200.0f);
  f("Steam", "cloudFormationHeightRatio", "Cloud height",
    c.steam.cloudFormationHeightRatio, 0.0f, 1.0f);
  f("Steam", "buoyancyBase", "Buoyancy", c.steam.buoyancyBase, -5.0f, 0.0f);
  f("Steam", "driftStrength", "Drift", c.steam.driftStrength, 0.0f, 1.0f);

  f("Earth", "fireHardnessLoss", "Hardness lost to fire",
    c.earth.fireHardnessLoss, 0.0f, 5.0f);
  f("Ice", "meltPoint", "Melt point", c.ice.meltPoint, -50.0f, 50.0f);
  i("Ice", "meltChance", "Melt chance (1 in)", c.ice.meltChance, 1, 50);

  for (size_t e = 0; e < c.elements.size(); ++e) {
    if (static_cast<Element>(e) == Element::AIR)
      continue;
    ElementProperties &p = c.elements[e];
    const char *g = ElementGroup(e);
    f(g, "density", "Density", p.density, 0.0f, 5000.0f);
    f(g, "defaultTemperature", "Temperature", p.defaultTemperature, -100.0f,
      2000.0f);
    f(g, "defaultMass", "Mass", p.defaultMass, 0.0f, 10.0f);
    f(g, "defaultHardness", "Hardness", p.defaultHardness, 0.0f, 1000.0f);
    f(g, "defaultLifetime", "Lifetime", p.defaultLifetime, 0.0f, 60.0f);
    f(g, "defaultMoisture", "Moisture", p.defaultMoisture, 0.0f, 10.0f);
    f(g, "heatCapacity", "Heat capacity", p.thermal.heatCapacity, 0.01f,
      10.0f);
    f(g, "conductivity", "Conductivity", p.thermal.conductivity, 0.0f, 1.0f);
    f(g, "coolingRate", "Cooling rate", p.thermal.coolingRate, 0.0f, 1.0f);
    f(g, "flammability", "Flammability", p.flammability, 0.0f, 1.0f);
    f(g, "ignitionTemp", "Ignition temperature", p.ignitionTemp, 0.0f,
      1000.0f);
  }
}

json ConfigDiff(const SimulationConfig &config) {
  // Walk the defaults and the config side by side; the field order is fixed
  SimulationConfig current = config, defaults;
  std::vector<std::pair<ConfigField, void *>> mine, base;
  ForEachConfigField(current, [&](const ConfigField &f, void *v) {
    mine.emplace_back(f, v);
  });
  ForEachConfigField(defaults, [&](const ConfigField &f, void *v) {
    base.emplace_back(f, v);
  });

  json diff = json::object();
  for (size_t n = 0; n < mine.size(); ++n) {
    const auto &[field, value] = mine[n];
    void *def = base[n].second;
    switch (field.kind) {
    case Kind::Float:
      if (*static_cast<float *>(value) != *static_cast<float *>(def))
        diff[field.group][field.key] = *static_cast<float *>(value);
      break;
    case Kind::Int:
      if (*static_cast<int *>(value) != *static_cast<int *>(def))
        diff[field.group][field.key] = *static_cast<int *>(value);
      break;
    case Kind::Bool:
      if (*static_cast<bool *>(value) != *static_cast<bool *>(def))
        diff[field.group][field.key] = *static_cast<bool *>(value);
      break;
    }
  }
  return diff;
}

SimulationConfig ConfigFromDiff(const json &diff) {
  SimulationConfig config;
  if (!diff.is_object())
    return config;
  ForEachConfigField(config, [&](const ConfigField &f, void *v) {
    auto group = diff.find(f.group);
    if (group == diff.end() || !group->is_object())
      return;
    auto it = group->find(f.key);
    if (it == group->end())
      return;
    switch (f.kind) {
    case Kind::Float:
      if (it->is_number() && std::isfinite(it->get<float>()))
        *static_cast<float *>(v) = it->get<float>();
      break;
    case Kind::Int:
      if (it->is_number_integer())
        *static_cast<int *>(v) = it->get<int>();
      break;
    case Kind::Bool:
      if (it->is_boolean())
        *static_cast<bool *>(v) = it->get<bool>();
      break;
    }
  });
  return config;
}
