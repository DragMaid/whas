#pragma once
#include "whas/spell/spell_system.h"
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <vector>

// Spell stats as integers, the form the server hands out. Both lockstep
// clients run the simulation on Dequantize() of these exact numbers, so it
// doesn't matter that the server (C#) and the editor (C++) compute the float
// stats with slightly different trig. See docs/protocol.md.
namespace SpellQuant {

constexpr int32_t STAT_SCALE = 1024;   // speed, range, power, ... in 1/1024
constexpr int32_t ANGLE_SCALE = 65536; // radians in 1/65536
constexpr int32_t AIM_SCALE = 16384;   // unit aim vectors in 1/16384

// Bump whenever SpellSystem::Evaluate or its tuning changes; the server keeps
// stats per evaluator version so old replays still reproduce
constexpr int EVALUATOR_VERSION = 9;

struct Stats {
  bool valid = false;
  uint8_t kind = 0;    // SpellKind
  uint8_t element = 0; // Element
  int32_t imbalance = 0;
  int32_t offset = 0; // ANGLE_SCALE
  int32_t speed = 0;
  int32_t range = 0;
  int32_t density = 0;
  int32_t power = 0;
  int32_t diameter = 0;
  int32_t particleCount = 0; // plain count
  int32_t temperature = 0;
  int32_t launchSpeed = 0;
  int32_t force = 0;
  int32_t duration = 0;
  uint8_t shape = 0; // SpellShape
  int32_t temperatureDelta = 0;
  int32_t hardnessScale = STAT_SCALE;
  int32_t crush = 0;
  int32_t restore = 0;
  int32_t collectRadius = 0;
  int32_t collectMax = 0;  // plain count
  int32_t pull = 0;
  int32_t flashRadius = 0;
  int32_t flashTime = 0;
  uint8_t homeTarget = 0;  // HomeTarget
  uint8_t homeElement = 0; // Element
  int32_t homeTurnRate = 0;
  int32_t homeRadius = 0;
  int32_t steerTime = 0;
  int32_t steerRate = 0;
  int32_t holdTime = 0;
  int32_t holdLength = 0;
  int32_t holdWidth = 0;
  int32_t holdRise = 0;
  std::vector<Stats> parts; // layered spells

  bool operator==(const Stats &) const;
};

Stats Quantize(const SpellStats &stats);
SpellStats Dequantize(const Stats &q);

// What a client uses offline: the same rounding the server applies
inline SpellStats Canonical(const Spell &spell) {
  return Dequantize(Quantize(SpellSystem::Evaluate(spell)));
}

struct Aim {
  int16_t x = 0;
  int16_t y = 0;
  bool operator==(const Aim &) const = default;
};

Aim QuantizeAim(Vector2 unit);
Vector2 DequantizeAim(Aim aim);
// Snap a float aim to the grid both clients will see
inline Vector2 SnapAim(Vector2 unit) { return DequantizeAim(QuantizeAim(unit)); }

void to_json(nlohmann::json &j, const Stats &s);
void from_json(const nlohmann::json &j, Stats &s);

} // namespace SpellQuant
