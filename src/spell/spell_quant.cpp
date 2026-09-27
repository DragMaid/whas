#include "whas/spell/spell_quant.h"
#include <cmath>
#include <nlohmann/json.hpp>

namespace SpellQuant {

namespace {

// Round half away from zero, the same as C#'s MidpointRounding.AwayFromZero
int32_t Q(float v, int32_t scale) {
  return static_cast<int32_t>(std::lround(static_cast<double>(v) * scale));
}

float D(int32_t v, int32_t scale) {
  return static_cast<float>(static_cast<double>(v) / scale);
}

} // namespace

Stats Quantize(const SpellStats &s) {
  Stats q;
  q.valid = s.valid;
  q.kind = static_cast<uint8_t>(s.kind);
  q.element = static_cast<uint8_t>(s.element);
  q.imbalance = Q(s.imbalance, STAT_SCALE);
  q.offset = Q(s.offsetRad, ANGLE_SCALE);
  q.speed = Q(s.speed, STAT_SCALE);
  q.range = Q(s.range, STAT_SCALE);
  q.density = Q(s.density, STAT_SCALE);
  q.power = Q(s.power, STAT_SCALE);
  q.diameter = Q(s.diameter, STAT_SCALE);
  q.particleCount = s.particleCount;
  q.temperature = Q(s.temperature, STAT_SCALE);
  q.launchSpeed = Q(s.launchSpeed, STAT_SCALE);
  q.force = Q(s.force, STAT_SCALE);
  q.duration = Q(s.duration, STAT_SCALE);
  return q;
}

SpellStats Dequantize(const Stats &q) {
  SpellStats s;
  s.valid = q.valid;
  s.kind = static_cast<SpellKind>(q.kind);
  s.element = static_cast<Element>(q.element);
  s.imbalance = D(q.imbalance, STAT_SCALE);
  s.offsetRad = D(q.offset, ANGLE_SCALE);
  s.speed = D(q.speed, STAT_SCALE);
  s.range = D(q.range, STAT_SCALE);
  s.density = D(q.density, STAT_SCALE);
  s.power = D(q.power, STAT_SCALE);
  s.diameter = D(q.diameter, STAT_SCALE);
  s.particleCount = q.particleCount;
  s.temperature = D(q.temperature, STAT_SCALE);
  s.launchSpeed = D(q.launchSpeed, STAT_SCALE);
  s.force = D(q.force, STAT_SCALE);
  s.duration = D(q.duration, STAT_SCALE);
  // Only used for the editor's balance display; the direction comes from
  // offsetRad
  s.netLocal = {0.0f, 0.0f};
  s.totalMagnitude = 0.0f;
  return s;
}

Aim QuantizeAim(Vector2 unit) {
  float len = std::hypot(unit.x, unit.y);
  if (!(len > 0.0f) || !std::isfinite(len))
    return {AIM_SCALE, 0};
  return {static_cast<int16_t>(std::lround(unit.x / len * (AIM_SCALE - 1))),
          static_cast<int16_t>(std::lround(unit.y / len * (AIM_SCALE - 1)))};
}

Vector2 DequantizeAim(Aim aim) {
  return {D(aim.x, AIM_SCALE - 1), D(aim.y, AIM_SCALE - 1)};
}

void to_json(nlohmann::json &j, const Stats &s) {
  j = nlohmann::json{{"valid", s.valid},
                     {"kind", s.kind},
                     {"element", s.element},
                     {"imbalance", s.imbalance},
                     {"offset", s.offset},
                     {"speed", s.speed},
                     {"range", s.range},
                     {"density", s.density},
                     {"power", s.power},
                     {"diameter", s.diameter},
                     {"particleCount", s.particleCount},
                     {"temperature", s.temperature},
                     {"launchSpeed", s.launchSpeed},
                     {"force", s.force},
                     {"duration", s.duration}};
}

void from_json(const nlohmann::json &j, Stats &s) {
  s.valid = j.at("valid").get<bool>();
  s.kind = j.at("kind").get<uint8_t>();
  s.element = j.at("element").get<uint8_t>();
  s.imbalance = j.at("imbalance").get<int32_t>();
  s.offset = j.at("offset").get<int32_t>();
  s.speed = j.at("speed").get<int32_t>();
  s.range = j.at("range").get<int32_t>();
  s.density = j.at("density").get<int32_t>();
  s.power = j.at("power").get<int32_t>();
  s.diameter = j.at("diameter").get<int32_t>();
  s.particleCount = j.at("particleCount").get<int32_t>();
  s.temperature = j.at("temperature").get<int32_t>();
  s.launchSpeed = j.at("launchSpeed").get<int32_t>();
  s.force = j.at("force").get<int32_t>();
  s.duration = j.at("duration").get<int32_t>();
}

} // namespace SpellQuant
