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

bool Stats::operator==(const Stats &o) const {
  return valid == o.valid && kind == o.kind && element == o.element &&
         imbalance == o.imbalance && offset == o.offset && speed == o.speed &&
         range == o.range && density == o.density && power == o.power &&
         diameter == o.diameter && particleCount == o.particleCount &&
         temperature == o.temperature && launchSpeed == o.launchSpeed &&
         force == o.force && duration == o.duration && shape == o.shape &&
         temperatureDelta == o.temperatureDelta &&
         hardnessScale == o.hardnessScale && crush == o.crush &&
         restore == o.restore && collectRadius == o.collectRadius &&
         collectMax == o.collectMax && pull == o.pull &&
         flashRadius == o.flashRadius && flashTime == o.flashTime &&
         homeTarget == o.homeTarget && homeElement == o.homeElement &&
         homeTurnRate == o.homeTurnRate && homeRadius == o.homeRadius &&
         steerTime == o.steerTime && steerRate == o.steerRate &&
         holdTime == o.holdTime && holdLength == o.holdLength &&
         holdWidth == o.holdWidth && holdRise == o.holdRise &&
         parts == o.parts;
}

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
  q.shape = static_cast<uint8_t>(s.shape);
  q.temperatureDelta = Q(s.temperatureDelta, STAT_SCALE);
  q.hardnessScale = Q(s.hardnessScale, STAT_SCALE);
  q.crush = Q(s.crush, STAT_SCALE);
  q.restore = Q(s.restore, STAT_SCALE);
  q.collectRadius = Q(s.collectRadius, STAT_SCALE);
  q.collectMax = s.collectMax;
  q.pull = Q(s.pull, STAT_SCALE);
  q.flashRadius = Q(s.flashRadius, STAT_SCALE);
  q.flashTime = Q(s.flashTime, STAT_SCALE);
  q.homeTarget = static_cast<uint8_t>(s.homeTarget);
  q.homeElement = static_cast<uint8_t>(s.homeElement);
  q.homeTurnRate = Q(s.homeTurnRate, STAT_SCALE);
  q.homeRadius = Q(s.homeRadius, STAT_SCALE);
  q.steerTime = Q(s.steerTime, STAT_SCALE);
  q.steerRate = Q(s.steerRate, STAT_SCALE);
  q.holdTime = Q(s.holdTime, STAT_SCALE);
  q.holdLength = Q(s.holdLength, STAT_SCALE);
  q.holdWidth = Q(s.holdWidth, STAT_SCALE);
  q.holdRise = Q(s.holdRise, STAT_SCALE);
  for (const SpellStats &part : s.parts)
    q.parts.push_back(Quantize(part));
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
  s.shape = static_cast<SpellShape>(q.shape);
  s.temperatureDelta = D(q.temperatureDelta, STAT_SCALE);
  s.hardnessScale = D(q.hardnessScale, STAT_SCALE);
  s.crush = D(q.crush, STAT_SCALE);
  s.restore = D(q.restore, STAT_SCALE);
  s.collectRadius = D(q.collectRadius, STAT_SCALE);
  s.collectMax = q.collectMax;
  s.pull = D(q.pull, STAT_SCALE);
  s.flashRadius = D(q.flashRadius, STAT_SCALE);
  s.flashTime = D(q.flashTime, STAT_SCALE);
  s.homeTarget = static_cast<HomeTarget>(q.homeTarget);
  s.homeElement = static_cast<Element>(q.homeElement);
  s.homeTurnRate = D(q.homeTurnRate, STAT_SCALE);
  s.homeRadius = D(q.homeRadius, STAT_SCALE);
  s.steerTime = D(q.steerTime, STAT_SCALE);
  s.steerRate = D(q.steerRate, STAT_SCALE);
  s.holdTime = D(q.holdTime, STAT_SCALE);
  s.holdLength = D(q.holdLength, STAT_SCALE);
  s.holdWidth = D(q.holdWidth, STAT_SCALE);
  s.holdRise = D(q.holdRise, STAT_SCALE);
  for (const Stats &part : q.parts)
    s.parts.push_back(Dequantize(part));
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
  // Modifier fields only when set, so plain spells keep their old form
  if (s.shape != 0)
    j["shape"] = s.shape;
  if (s.temperatureDelta != 0)
    j["temperatureDelta"] = s.temperatureDelta;
  if (s.hardnessScale != STAT_SCALE)
    j["hardnessScale"] = s.hardnessScale;
  if (s.crush != 0)
    j["crush"] = s.crush;
  if (s.restore != 0)
    j["restore"] = s.restore;
  if (s.collectRadius != 0)
    j["collectRadius"] = s.collectRadius;
  if (s.collectMax != 0)
    j["collectMax"] = s.collectMax;
  if (s.pull != 0)
    j["pull"] = s.pull;
  if (s.flashRadius != 0)
    j["flashRadius"] = s.flashRadius;
  if (s.flashTime != 0)
    j["flashTime"] = s.flashTime;
  if (s.homeTarget != 0) {
    j["homeTarget"] = s.homeTarget;
    j["homeElement"] = s.homeElement;
    j["homeTurnRate"] = s.homeTurnRate;
    j["homeRadius"] = s.homeRadius;
  }
  if (s.steerTime != 0) {
    j["steerTime"] = s.steerTime;
    j["steerRate"] = s.steerRate;
  }
  if (s.holdTime != 0) {
    j["holdTime"] = s.holdTime;
    j["holdLength"] = s.holdLength;
    j["holdWidth"] = s.holdWidth;
  }
  if (s.holdRise != 0)
    j["holdRise"] = s.holdRise;
  if (!s.parts.empty()) {
    j["parts"] = nlohmann::json::array();
    for (const Stats &part : s.parts) {
      nlohmann::json pj;
      to_json(pj, part);
      j["parts"].push_back(std::move(pj));
    }
  }
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
  s.shape = j.value("shape", uint8_t{0});
  s.temperatureDelta = j.value("temperatureDelta", int32_t{0});
  s.hardnessScale = j.value("hardnessScale", STAT_SCALE);
  s.crush = j.value("crush", int32_t{0});
  s.restore = j.value("restore", int32_t{0});
  s.collectRadius = j.value("collectRadius", int32_t{0});
  s.collectMax = j.value("collectMax", int32_t{0});
  s.pull = j.value("pull", int32_t{0});
  s.flashRadius = j.value("flashRadius", int32_t{0});
  s.flashTime = j.value("flashTime", int32_t{0});
  s.homeTarget = j.value("homeTarget", uint8_t{0});
  s.homeElement = j.value("homeElement", uint8_t{0});
  s.homeTurnRate = j.value("homeTurnRate", int32_t{0});
  s.homeRadius = j.value("homeRadius", int32_t{0});
  s.steerTime = j.value("steerTime", int32_t{0});
  s.steerRate = j.value("steerRate", int32_t{0});
  s.holdTime = j.value("holdTime", int32_t{0});
  s.holdLength = j.value("holdLength", int32_t{0});
  s.holdWidth = j.value("holdWidth", int32_t{0});
  s.holdRise = j.value("holdRise", int32_t{0});
  s.parts.clear();
  if (auto it = j.find("parts"); it != j.end())
    for (const auto &pj : *it) {
      Stats part;
      from_json(pj, part);
      s.parts.push_back(std::move(part));
    }
}

} // namespace SpellQuant
