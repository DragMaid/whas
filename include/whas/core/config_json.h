#pragma once
#include "whas/core/config.h"
#include <functional>
#include <nlohmann/json_fwd.hpp>

// Every tunable SimulationConfig value with a name, a group and a sensible
// slider range. Maps store their settings as the difference from the
// defaults, and the map editor's settings tree walks the same list.
struct ConfigField {
  enum class Kind { Float, Int, Bool };
  const char *group; // "World", "Cloud", "Water", "Element/FIRE", ...
  const char *key;   // unique within its group
  const char *label;
  Kind kind;
  float min = 0.0f;
  float max = 1.0f;
};

void ForEachConfigField(
    SimulationConfig &config,
    const std::function<void(const ConfigField &, void *value)> &visit);

// Only the values that differ from SimulationConfig{}: {"Cloud": {"rainChance": 30}}
nlohmann::json ConfigDiff(const SimulationConfig &config);
// Defaults with the diff applied; unknown keys are ignored
SimulationConfig ConfigFromDiff(const nlohmann::json &diff);
