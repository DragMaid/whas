#include "whas/spell/spell_system.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/particle_system.h"
#include "whas/physics/rigid_body_system.h"
#include "whas/world/grid.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace {

// All spell balancing knobs in one place
struct SpellTuning {
  float baseSpeed = 40.0f;       // cells/s with no signs
  float speedPerSign = 15.0f;    // cells/s per unit of sign scale
  float lateralSpeedLoss = 0.5f; // fraction of speed lost at full sideways pull
  float flightTime = 0.8f;       // range = speed * flightTime
  float maxOffsetDeg = 60.0f;
  // power = 0.5 * density * speed^2 * powerScale. Speed does the piercing:
  // a slow spell (one small sign) can't break earth (120) whatever its sigil;
  // water only ever knocks out one cell per particle before splashing; rock
  // (250) needs a fast or heavy (earth/rock) spell.
  float powerScale = 0.02f;
  float densityBase = 0.8f;      // sigil scale mostly adds volume;
  float densityPerSigil = 0.2f;  // it only thickens the element a little
  float baseDiameter = 2.0f;
  float diameterPerSigilScale = 4.0f;
  float maxDiameter = 12.0f;
  int baseParticles = 20;
  float particlesPerSigilScale = 40.0f;
  int particlesPerSign = 4;
  int maxParticles = 200;

  float fireBaseTemp = 1200.0f;   // fire spells land this hot...
  float fireTempPerSigil = 800.0f; // ...plus this per unit of sigil scale

  // Flight: caster speed = spell speed * (base + perSigil * sigil scale)
  float launchBase = 0.35f;
  float launchPerSigil = 0.35f;
  float maxLaunchSpeed = 160.0f;

  // Field (pulling sign): force = speed * (0.5 + sigil) * |pull| * this.
  // Four balanced signs, a full-size sigil and one pulling sign give ~90:
  // rock (mass 3) is shoved to ~20 cells/s over the field, water (mass 1)
  // to ~60, a character (mass 3) like the rock.
  float gustForcePerSpeed = 0.6f;
  float minPull = 0.25f; // summed pulling sign scale, clamped
  float maxPull = 2.5f;
  float gustBaseDuration = 0.35f;
  float gustDurationPerSigil = 0.3f;
  float gustWidthScale = 1.5f; // field width relative to the beam diameter
  float gustLiftAccel = 40.0f; // cells need this much push to be picked up
  float gustLiftChance = 0.25f;
  float gustLiftVelocity = 0.1f; // lifted cell starts at accel * this

  // Modifier signs; each scales with the summed scale of its signs
  float convergenceDensity = 0.5f; // denser...
  float convergenceNarrow = 0.4f;  // ...narrower...
  float convergenceHardness = 0.25f; // ...a little harder when it lands...
  float convergenceSpeed = 0.25f;    // ...faster (so it reaches further)...
  float convergenceParticles = 0.3f; // ...and less of it
  float convergenceLaunch = 0.3f;    // wind underfoot throws harder
  float convergenceForce = 0.5f;     // a field pushes harder, narrower
  float expansionDiameter = 0.4f;
  float maxExpandedDiameter = 20.0f;
  float strengthHardness = 0.6f;
  float coolingPerSign = 400.0f; // degrees per unit of cooling sign scale
  float waterFreezeDrop = 15.0f; // water (15 C) cooled this much is ice
  float fireIgniteTemp = 800.0f; // fire cooled below this can't ignite
  float earthToRockHardness = 1.5f; // earth hardened this much is rock
  float collectBaseRadius = 6.0f;
  float collectRadiusPerSign = 10.0f;
  float collectCellsPerSign = 40.0f;
  int maxCollect = 150;
  // Light: much faster than matter, few motes, each bursting into a flash
  // that blinds anyone (caster included) within its radius
  float lightSpeedScale = 2.5f;
  float lightParticleScale = 0.25f;
  float flashBaseRadius = 8.0f;
  float flashRadiusPerSigil = 10.0f;
  float flashBaseTime = 1.5f; // seconds of blindness
  float flashTimePerSigil = 1.5f;
  float maxFlashRadius = 30.0f;
  // Guidance: a bigger guidance sigil turns harder and looks further
  float homeBaseTurn = 2.5f; // rad/s
  float homeTurnPerSigil = 1.5f;
  float maxHomeTurn = 6.0f;
  float homeBaseRadius = 40.0f;
  float homeRadiusPerSigil = 30.0f;
  float maxHomeRadius = 120.0f;
  // Sights set: more or bigger signs follow the cursor for longer and turn
  // a little quicker, but always heavily
  float steerBaseTime = 0.4f;
  float steerTimePerSign = 0.5f;
  float maxSteerTime = 3.0f;
  float steerBaseRate = 1.6f; // rad/s, about 90 degrees a second
  float steerRatePerSign = 0.2f;
  // Column: the element is held as a block for a while, then let go
  float holdBaseTime = 1.5f; // seconds
  float holdTimePerSign = 1.0f;
  float maxHoldTime = 6.0f;
  int minHoldWidth = 2;
  int maxHoldWidth = 24;
  int maxHoldLength = 120;
  float mendsPerSign = 3.0f; // cells repetition mends per tick
  float maxSteerRate = 2.5f;
  // Layered spells: an embedded spell of scale s is worth s / this; one
  // that fills the whole core is worth all of it
  float componentFullScale = 0.7f;
  float componentMinEffect = 0.5f;
  float componentMaxEffect = 1.0f;

};

constexpr SpellTuning kTuning;

float Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }

} // namespace

SpellKind SpellSystem::SigilKind(const std::string &assetId) {
  if (assetId == "wind_underfoot")
    return SpellKind::Flight;
  if (assetId == "wind")
    return SpellKind::Field;
  if (SigilElement(assetId) != Element::AIR)
    return SpellKind::Element;
  return SpellKind::None;
}

Element SpellSystem::SigilElement(const std::string &assetId) {
  if (assetId == "water")
    return Element::WATER;
  if (assetId == "fire")
    return Element::FIRE;
  if (assetId == "earth")
    return Element::EARTH;
  if (assetId == "ice")
    return Element::ICE;
  if (assetId == "sand")
    return Element::SAND;
  if (assetId == "rock")
    return Element::ROCK;
  if (assetId == "light")
    return Element::LIGHT;
  return Element::AIR;
}

// Mass per particle of each element when carried by a spell
static float SpellDensity(Element element) {
  switch (element) {
  case Element::WATER:
    return 1.0f;
  case Element::ICE:
    return 0.9f;
  case Element::SAND:
    return 1.6f;
  case Element::EARTH:
    return 2.0f;
  case Element::ROCK:
    return 2.5f;
  case Element::FIRE:
    return 0.2f;
  case Element::STEAM:
    return 0.1f;
  case Element::CLOUD:
    return 0.05f;
  case Element::LIGHT:
    return 0.0f; // weightless: it breaks nothing
  default:
    return 1.0f;
  }
}

bool SpellSystem::IsShapeSigil(const std::string &assetId) {
  return SpellShapes::TriggerFor(assetId, GlyphKind::Sigil) != nullptr;
}

bool SpellSystem::SignInvertible(const std::string &assetId) {
  return assetId == "crushing" || assetId == "expansion" ||
         assetId == "pulling";
}

bool SpellStats::HasFlight() const {
  if (kind == SpellKind::Flight)
    return true;
  return std::any_of(parts.begin(), parts.end(),
                     [](const SpellStats &p) { return p.HasFlight(); });
}

float SpellSystem::ComponentEffectiveness(float scale) {
  return std::clamp(scale / kTuning.componentFullScale,
                    kTuning.componentMinEffect, kTuning.componentMaxEffect);
}

namespace {

// Summed scales of the modifier signs in a circle (crushing and expansion
// count negative when inverted)
struct Modifiers {
  float convergence = 0.0f;
  float crush = 0.0f;
  float repetition = 0.0f;
  float cooling = 0.0f;
  float strengthening = 0.0f;
  float collection = 0.0f;
  float expansion = 0.0f;
  float pull = 0.0f; // pulling signs, negative when inverted (pushing)
  float sights = 0.0f;
  float column = 0.0f;
  // Summed scales of each shape's trigger glyphs
  std::array<float, static_cast<size_t>(SpellShape::Count)> shapes{};

  Modifiers operator+(const Modifiers &o) const {
    Modifiers m{convergence + o.convergence, crush + o.crush,
                repetition + o.repetition,   cooling + o.cooling,
                strengthening + o.strengthening, collection + o.collection,
                expansion + o.expansion,     pull + o.pull,
                sights + o.sights,           column + o.column, {}};
    for (size_t i = 0; i < shapes.size(); ++i)
      m.shapes[i] = shapes[i] + o.shapes[i];
    return m;
  }
};

// What one circle's glyphs add up to
struct Circle {
  int sigilCount = 0; // sigils that pick the kind (the fired one)
  int shapeSigils = 0;
  int allSigils = 0; // every sigil, for rings that may hold none

  // Guidance: its summed scale, what it chases, and whether the sigils
  // around it make sense (one guidance, one target, at most one human)
  float guidance = 0.0f;
  HomeTarget homeTarget = HomeTarget::None;
  Element homeElement = Element::AIR;
  bool guideValid = true;
  float sigilScale = 0.0f;
  SpellKind kind = SpellKind::None;
  Element element = Element::AIR;

  // Column (thrust) signs
  Vector2 net{0.0f, 0.0f};
  float magnitude = 0.0f;
  int thrustSigns = 0;

  Modifiers mods;
};

Circle ReadCircle(const std::vector<PlacedGlyph> &glyphs) {
  Circle c;
  int guidanceSigils = 0, humanSigils = 0;
  std::vector<const PlacedGlyph *> sigils; // element and kind sigils
  for (const auto &glyph : glyphs) {
    const std::string &id = glyph.assetId;
    if (const ShapeTrigger *t = SpellShapes::TriggerFor(id, glyph.kind)) {
      c.mods.shapes[static_cast<size_t>(t->shape)] += glyph.scale;
      if (glyph.kind == GlyphKind::Sigil)
        c.shapeSigils++;
      continue;
    }
    if (glyph.kind == GlyphKind::Sigil) {
      c.allSigils++;
      if (id == "guidance") {
        guidanceSigils++;
        c.guidance += glyph.scale;
      } else if (id == "human") {
        humanSigils++;
      } else {
        sigils.push_back(&glyph);
      }
      continue;
    }

    float sign = glyph.inverted ? -glyph.scale : glyph.scale;
    Modifiers &m = c.mods;
    if (id == "convergence")
      m.convergence += glyph.scale;
    else if (id == "crushing")
      m.crush += sign;
    else if (id == "repetition")
      m.repetition += glyph.scale;
    else if (id == "cooling")
      m.cooling += glyph.scale;
    else if (id == "strengthening")
      m.strengthening += glyph.scale;
    else if (id == "collection")
      m.collection += glyph.scale;
    else if (id == "expansion")
      m.expansion += sign;
    else if (id == "pulling")
      m.pull += sign;
    else if (id == "sights_set")
      m.sights += glyph.scale;
    else if (id == "column")
      m.column += glyph.scale;
    else if (id == "levitation") {
      // Levitation is thrust: sign glyphs point up in their SVG; rotate like
      // SpellGeometry does
      float rad = glyph.rotationDeg * DEG2RAD;
      Vector2 forward{std::sin(rad), -std::cos(rad)};
      c.net.x += forward.x * glyph.scale;
      c.net.y += forward.y * glyph.scale;
      c.magnitude += glyph.scale;
      c.thrustSigns++;
    }
  }

  // Which sigil is fired. With guidance, a human sigil or a second element
  // sigil is what it chases: the bigger element sigil fires (the first on a
  // tie), the smaller one is the target.
  const PlacedGlyph *fired = sigils.size() == 1 ? sigils[0] : nullptr;
  if (c.guidance > 0.0f && sigils.size() == 2 && humanSigils == 0) {
    bool second = sigils[1]->scale > sigils[0]->scale;
    fired = sigils[second ? 1 : 0];
    c.homeTarget = HomeTarget::Element;
    c.homeElement = SpellSystem::SigilElement(sigils[second ? 0 : 1]->assetId);
  } else if (c.guidance > 0.0f && humanSigils > 0) {
    c.homeTarget = HomeTarget::Human;
  }
  c.sigilCount = fired ? 1 : static_cast<int>(sigils.size());
  if (fired) {
    c.sigilScale = fired->scale;
    c.kind = SpellSystem::SigilKind(fired->assetId);
    c.element = SpellSystem::SigilElement(fired->assetId);
  }
  // Guidance needs something to chase (never plain air), and a human sigil
  // means nothing without guidance
  bool targeted = c.homeTarget == HomeTarget::Human ||
                  (c.homeTarget == HomeTarget::Element &&
                   c.homeElement != Element::AIR);
  c.guideValid = guidanceSigils <= 1 && humanSigils <= 1 &&
                 (c.guidance > 0.0f ? targeted : humanSigils == 0);
  return c;
}

// How a circle's levitation signs steer and speed up the spell
struct Thrust {
  float imbalance = 0.0f;
  float offsetRad = 0.0f;
  float speedGain = 0.0f; // cells/s on top of the base speed
};

Thrust ReadThrust(const Circle &c) {
  Thrust t;
  float lateral = c.net.x;
  float forward = -c.net.y;
  float lateralRatio = 0.0f;
  if (c.magnitude > 0.0f) {
    t.imbalance =
        std::min(1.0f, std::hypot(c.net.x, c.net.y) / c.magnitude);
    lateralRatio = std::min(1.0f, std::abs(lateral) / c.magnitude);
    float maxOffset = kTuning.maxOffsetDeg * DEG2RAD;
    t.offsetRad =
        std::clamp(std::atan2(lateral, std::max(0.0f, c.magnitude + forward)),
                   -maxOffset, maxOffset);
  }
  t.speedGain = kTuning.speedPerSign * c.magnitude *
                (1.0f - kTuning.lateralSpeedLoss * lateralRatio);
  return t;
}

// Grow (e > 0) or shrink (e < 0) a width
float Expand(float value, float e) {
  if (e == 0.0f)
    return value;
  float f = 1.0f + kTuning.expansionDiameter * std::abs(e);
  return e > 0.0f ? value * f : value / f;
}

// The stats of one circle. `mods` are the modifiers that apply to it (its
// own, plus the outer ring's in a layered spell), `effect` scales how much it
// fires, and the outer ring adds speed and turns it.
SpellStats Build(const Circle &c, const Modifiers &mods, float effect,
                 float speedBonus, float offsetBonus) {
  SpellStats s;
  Thrust thrust = ReadThrust(c);
  s.valid = c.sigilCount == 1 && c.kind != SpellKind::None &&
            c.shapeSigils <= 1 && c.guideValid;
  s.kind = c.kind;
  s.element = c.element;
  // Pulling signs make a field out of the sigil's element: they move what's
  // already there instead of making more. The wind sigil only ever moves
  // (a field of everything); wind underfoot and light can't be pulled.
  if (mods.pull != 0.0f) {
    if ((s.kind == SpellKind::Element && s.element != Element::LIGHT) ||
        s.kind == SpellKind::Field)
      s.kind = SpellKind::Field;
    else
      s.valid = false;
  } else if (s.kind == SpellKind::Field) {
    s.valid = false;
  }
  s.netLocal = c.net;
  s.totalMagnitude = c.magnitude;
  s.imbalance = thrust.imbalance;
  s.offsetRad = thrust.offsetRad + offsetBonus;

  float sigilScale = c.sigilScale;
  s.speed = kTuning.baseSpeed + thrust.speedGain + speedBonus;
  s.range = s.speed * kTuning.flightTime;

  s.diameter = std::clamp(
      kTuning.baseDiameter + sigilScale * kTuning.diameterPerSigilScale, 1.0f,
      kTuning.maxDiameter);

  // Only something fired can be guided
  if (c.homeTarget != HomeTarget::None) {
    if (s.kind != SpellKind::Element)
      s.valid = false;
    s.homeTarget = c.homeTarget;
    s.homeElement = c.homeElement;
    s.homeTurnRate = std::min(kTuning.maxHomeTurn,
                              kTuning.homeBaseTurn +
                                  kTuning.homeTurnPerSigil * c.guidance);
    s.homeRadius = std::min(kTuning.maxHomeRadius,
                            kTuning.homeBaseRadius +
                                kTuning.homeRadiusPerSigil * c.guidance);
  }

  switch (s.kind) {
  case SpellKind::Element: {
    s.density = SpellDensity(s.element) *
                (kTuning.densityBase + kTuning.densityPerSigil * sigilScale);
    int count = kTuning.baseParticles +
                static_cast<int>(sigilScale * kTuning.particlesPerSigilScale) +
                c.thrustSigns * kTuning.particlesPerSign;
    if (s.element == Element::FIRE)
      s.temperature =
          kTuning.fireBaseTemp + kTuning.fireTempPerSigil * sigilScale;
    if (s.element == Element::LIGHT) {
      s.speed *= kTuning.lightSpeedScale;
      s.range = s.speed * kTuning.flightTime;
      count = std::max(1, static_cast<int>(count * kTuning.lightParticleScale));
      s.flashRadius =
          std::min(kTuning.maxFlashRadius,
                   kTuning.flashBaseRadius +
                       kTuning.flashRadiusPerSigil * sigilScale);
      s.flashTime = kTuning.flashBaseTime + kTuning.flashTimePerSigil * sigilScale;
    }

    if (mods.convergence > 0.0f) {
      s.density *= 1.0f + kTuning.convergenceDensity * mods.convergence;
      s.diameter /= 1.0f + kTuning.convergenceNarrow * mods.convergence;
      s.hardnessScale *= 1.0f + kTuning.convergenceHardness * mods.convergence;
      s.speed *= 1.0f + kTuning.convergenceSpeed * mods.convergence;
      s.range = s.speed * kTuning.flightTime;
      count = static_cast<int>(
          count / (1.0f + kTuning.convergenceParticles * mods.convergence));
    }
    if (mods.expansion != 0.0f) {
      // Grows (or shrinks) both ways: the width, and the material with the
      // area so the shape stays as dense
      s.diameter = Expand(s.diameter, mods.expansion);
      float f = 1.0f + kTuning.expansionDiameter * std::abs(mods.expansion);
      float pf = f * f;
      count = static_cast<int>(mods.expansion > 0.0f ? count * pf
                                                     : count / pf);
    }
    s.diameter =
        std::clamp(s.diameter, 1.0f, kTuning.maxExpandedDiameter);
    // Shape glyphs: multipliers stack, the last listed shape wins
    for (const ShapeTrigger &t : SpellShapes::Triggers()) {
      float sum = mods.shapes[static_cast<size_t>(t.shape)];
      if (sum <= 0.0f)
        continue;
      s.shape = t.shape;
      count = static_cast<int>(count *
                               (t.particlesBase + t.particlesPerScale * sum));
    }
    if (mods.strengthening > 0.0f)
      s.hardnessScale *= 1.0f + kTuning.strengthHardness * mods.strengthening;
    if (mods.repetition > 0.0f) {
      // Lands as it naturally is, whatever else the circle says (cooling
      // included)
      s.restore = mods.repetition;
      s.temperature = 0.0f;
      s.hardnessScale = 1.0f;
    } else if (mods.cooling > 0.0f) {
      // Cooled as it's cast, so what flies out is already cold: water
      // freezes to ice, fire too cold to ignite is only smoke, and
      // anything else lands cold
      float drop = kTuning.coolingPerSign * mods.cooling;
      if (s.element == Element::FIRE) {
        s.temperature -= drop;
        if (s.temperature < kTuning.fireIgniteTemp) {
          s.element = Element::SMOKE;
          s.temperature = 0.0f;
        }
      } else {
        s.temperatureDelta = -drop;
        if (s.element == Element::WATER && drop >= kTuning.waterFreezeDrop)
          s.element = Element::ICE;
      }
    }
    // Earth pressed or strengthened hard enough is rock (the hardening
    // went into that)
    if (s.element == Element::EARTH &&
        s.hardnessScale >= kTuning.earthToRockHardness) {
      s.element = Element::ROCK;
      s.hardnessScale = 1.0f;
    }
    s.crush = mods.crush;
    if (mods.collection > 0.0f) {
      s.collectRadius = kTuning.collectBaseRadius +
                        kTuning.collectRadiusPerSign * mods.collection;
      s.collectMax = std::min(
          kTuning.maxCollect,
          static_cast<int>(kTuning.collectCellsPerSign * mods.collection));
    }

    if (mods.sights > 0.0f) {
      s.steerTime = std::min(kTuning.maxSteerTime,
                             kTuning.steerBaseTime +
                                 kTuning.steerTimePerSign * mods.sights);
      s.steerRate = std::min(kTuning.maxSteerRate,
                             kTuning.steerBaseRate +
                                 kTuning.steerRatePerSign * mods.sights);
    }

    s.particleCount = std::clamp(static_cast<int>(count * effect), 1,
                                 kTuning.maxParticles);
    // Column: a block as wide as the beam and long enough for the material.
    // Without levitation it doesn't fly: it forms in front of the caster.
    if (mods.column > 0.0f && s.element != Element::LIGHT) {
      s.holdTime = std::min(kTuning.maxHoldTime,
                            kTuning.holdBaseTime +
                                kTuning.holdTimePerSign * mods.column);
      int width = std::clamp(static_cast<int>(std::lround(s.diameter)),
                             kTuning.minHoldWidth, kTuning.maxHoldWidth);
      s.holdWidth = static_cast<float>(width);
      s.holdLength = static_cast<float>(std::clamp(
          (s.particleCount + width - 1) / width, 2, kTuning.maxHoldLength));
      if (c.magnitude <= 0.0f && speedBonus <= 0.0f) {
        s.speed = 0.0f;
        s.range = 0.0f;
      }
    }
    s.power = 0.5f * s.density * s.speed * s.speed * kTuning.powerScale;
    break;
  }
  case SpellKind::Flight:
    s.launchSpeed = std::min(
        kTuning.maxLaunchSpeed,
        s.speed * (kTuning.launchBase + kTuning.launchPerSigil * sigilScale) *
            effect * (1.0f + kTuning.convergenceLaunch * mods.convergence));
    break;
  case SpellKind::Field: {
    float pull = std::clamp(std::abs(mods.pull), kTuning.minPull,
                            kTuning.maxPull);
    s.pull = mods.pull > 0.0f ? pull : -pull;
    s.force = s.speed * (0.5f + sigilScale) * pull *
              kTuning.gustForcePerSpeed * effect;
    s.duration =
        kTuning.gustBaseDuration + kTuning.gustDurationPerSigil * sigilScale;
    s.diameter = Expand(s.diameter * kTuning.gustWidthScale, mods.expansion);
    if (mods.convergence > 0.0f) {
      s.diameter /= 1.0f + kTuning.convergenceNarrow * mods.convergence;
      s.force *= 1.0f + kTuning.convergenceForce * mods.convergence;
    }
    break;
  }
  default:
    break;
  }
  return s;
}

SpellStats EvaluateLayered(const Spell &spell) {
  Circle outer = ReadCircle(spell.glyphs);
  Thrust thrust = ReadThrust(outer);

  SpellStats s;
  s.kind = SpellKind::Compound;
  s.netLocal = outer.net;
  s.totalMagnitude = outer.magnitude;
  s.imbalance = thrust.imbalance;
  s.offsetRad = thrust.offsetRad;
  s.speed = kTuning.baseSpeed + thrust.speedGain;

  int count = static_cast<int>(spell.components.size());
  s.valid = outer.allSigils == 0 && outer.shapeSigils == 0 && count >= 1 &&
            count <= LAYER_HARD_MAX_COMPONENTS;
  for (const SpellComponent &component : spell.components) {
    Circle inner = ReadCircle(component.glyphs);
    SpellStats part =
        Build(inner, inner.mods + outer.mods,
              SpellSystem::ComponentEffectiveness(component.scale),
              thrust.speedGain, thrust.offsetRad +
                                    component.rotationDeg * DEG2RAD);
    s.valid = s.valid && part.valid;
    s.parts.push_back(std::move(part));
  }
  return s;
}

} // namespace

namespace {

std::string CircleProblem(const std::vector<PlacedGlyph> &glyphs) {
  Circle c = ReadCircle(glyphs);
  int guidance = 0, human = 0, signs = 0;
  for (const PlacedGlyph &g : glyphs) {
    guidance += g.assetId == "guidance";
    human += g.assetId == "human";
    signs += g.kind == GlyphKind::Sign;
  }
  if (c.shapeSigils > 1)
    return "Only one dragon sigil per spell.";
  if (guidance > 1 || human > 1)
    return "Only one guidance and one human sigil per spell.";
  if (human > 0 && guidance == 0)
    return "A human sigil is only a target: add a guidance sigil.";
  if (c.sigilCount == 0)
    return "Place a sigil: it's what the spell is made of.";
  if (c.sigilCount > 1)
    return guidance > 0
               ? "Guidance takes one target: a human sigil or a second, "
                 "smaller element sigil."
               : "One sigil per spell. Two only with guidance: the smaller "
                 "one is what it chases.";
  if (guidance > 0 && !c.guideValid)
    return "Guidance needs a target: a human sigil or a second, smaller "
           "element sigil.";
  if (c.kind == SpellKind::None)
    return "That sigil does nothing on its own.";
  bool pulled = c.mods.pull != 0.0f;
  if (c.kind == SpellKind::Field && !pulled)
    return "Wind only moves what's there: add a pulling sign.";
  if (pulled && (c.kind == SpellKind::Flight || c.element == Element::LIGHT))
    return "Pulling can't take hold of that sigil.";
  if (c.homeTarget != HomeTarget::None &&
      (pulled || c.kind != SpellKind::Element))
    return "Only fired spells (elements, light) can be guided.";
  if (signs == 0)
    return "Add at least one sign.";
  return "";
}

} // namespace

std::string SpellSystem::Problem(const Spell &spell) {
  if (!spell.Layered())
    return CircleProblem(spell.glyphs);
  for (const PlacedGlyph &g : spell.glyphs)
    if (g.kind == GlyphKind::Sigil)
      return "The outer ring holds signs only.";
  if ((int)spell.components.size() > LAYER_HARD_MAX_COMPONENTS)
    return "A layered spell holds at most 64 spells.";
  for (const SpellComponent &c : spell.components) {
    // Ring signs count too (a ring pulling sign makes every part a field)
    std::vector<PlacedGlyph> glyphs = c.glyphs;
    glyphs.insert(glyphs.end(), spell.glyphs.begin(), spell.glyphs.end());
    if (std::string p = CircleProblem(glyphs); !p.empty())
      return (c.source.empty() ? std::string("A part") : c.source) + ": " + p;
  }
  return "";
}

SpellStats SpellSystem::Evaluate(const Spell &spell) {
  if (spell.Layered())
    return EvaluateLayered(spell);
  Circle c = ReadCircle(spell.glyphs);
  return Build(c, c.mods, 1.0f, 0.0f, 0.0f);
}

Vector2 SpellSystem::ResolveDirection(const SpellStats &stats, Vector2 aim) {
  float c = std::cos(stats.offsetRad);
  float s = std::sin(stats.offsetRad);
  return {aim.x * c - aim.y * s, aim.x * s + aim.y * c};
}

Vector2 SpellSystem::FlightVelocity(const SpellStats &stats, Vector2 aim) {
  Vector2 v{0.0f, 0.0f};
  if (stats.kind == SpellKind::Flight) {
    Vector2 dir = ResolveDirection(stats, aim);
    v = {dir.x * stats.launchSpeed, dir.y * stats.launchSpeed};
  }
  for (const SpellStats &part : stats.parts) {
    Vector2 pv = FlightVelocity(part, aim);
    v.x += pv.x;
    v.y += pv.y;
  }
  return v;
}

float SpellSystem::FieldStrengthAt(const SpellEffect &field, Vector2 point) {
  Vector2 rel{point.x - field.origin.x, point.y - field.origin.y};
  float along = Dot(rel, field.direction);
  if (along < 0.0f || along > field.stats.range)
    return 0.0f;
  float across =
      std::abs(rel.x * field.direction.y - rel.y * field.direction.x);
  if (across > field.stats.diameter * 0.5f)
    return 0.0f;
  return 1.0f - 0.5f * (along / field.stats.range);
}

bool SpellSystem::FieldMoves(const SpellStats &field, Element element) {
  if (element == Element::LIGHT)
    return false; // nothing to take hold of
  return field.element == Element::AIR || field.element == element;
}

Vector2 SpellSystem::FieldPush(const SpellEffect &field) {
  // Pulling drags things back toward the caster
  float sign = field.stats.pull < 0.0f ? 1.0f : -1.0f;
  return {field.direction.x * sign, field.direction.y * sign};
}

namespace {

int TotalParticles(const SpellEffect &effect) {
  return effect.stats.particleCount + effect.bonusParticles;
}

// Particles of a set figure fly on for the rows still to come behind them
// (a row per cell), so the whole figure lets go at once: when its last row
// has flown the spell's range
float FigureExtraRange(const SpellEffect &effect) {
  const SpellStats &s = effect.stats;
  int rows = SpellShapes::FigureRows(SpellShapes::Get(s.shape), s.diameter);
  return rows > 0 ? static_cast<float>(std::max(0, rows - effect.rows)) : 0.0f;
}

// Whether a spell particle would form inside the ground (or off the world):
// that part of the figure is left out rather than stuck in the terrain
bool InsideGround(const ElementContext &ctx, Vector2 pos) {
  int x = static_cast<int>(std::floor(pos.x));
  int y = static_cast<int>(std::floor(pos.y));
  if (!ctx.grid.InBounds(x, y))
    return true;
  const Cell &c = ctx.grid.Get(x, y);
  const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];
  bool liquid = props.mobile && !props.solid;
  return c.element != Element::AIR && !props.passable && !liquid;
}

void SpawnSpellParticle(SpellEffect &effect, ElementContext &ctx, Vector2 pos,
                        Vector2 vel) {
  const SpellStats &s = effect.stats;
  float range = s.range + FigureExtraRange(effect);
  if (InsideGround(ctx, pos)) {
    effect.emitted++; // it still counts: the figure keeps its layout
    return;
  }
  if (Particle *p = ctx.particles.Spawn(pos, vel, s.element, range, s.power,
                                        true, effect.owner)) {
    p->castId = effect.castId;
    p->temperature = s.temperature;
    p->temperatureDelta = s.temperatureDelta;
    p->hardnessScale = s.hardnessScale;
    p->crush = s.crush;
    p->restore = s.restore;
    p->flashRadius = s.flashRadius;
    p->flashTime = s.flashTime;
    if (effect.guideId >= 0)
      ctx.particles.Follow(*p, effect.guideId);
  }
  effect.emitted++;
}

// One row across the beam: a particle at each offset (cells to the side of
// the aim line through `base`), all shifted `shift` cells, `ahead` cells
// along the aim
template <typename Offsets>
void EmitRow(SpellEffect &effect, ElementContext &ctx, Vector2 base,
             const Offsets &offsets, float scale, float shift,
             float ahead = 0.0f) {
  const SpellStats &s = effect.stats;
  Vector2 perp{-effect.direction.y, effect.direction.x};
  Vector2 vel{effect.direction.x * s.speed, effect.direction.y * s.speed};
  int total = TotalParticles(effect);
  for (float offset : offsets) {
    if (effect.emitted >= total)
      return;
    float lateral = offset * scale + shift;
    Vector2 d = effect.direction;
    SpawnSpellParticle(effect, ctx,
                       {base.x + perp.x * lateral + d.x * ahead,
                        base.y + perp.y * lateral + d.y * ahead},
                       vel);
  }
}

// Evenly spaced lanes across a beam `width` cells wide
void EmitLanes(SpellEffect &effect, ElementContext &ctx, float width,
               float shift) {
  int lanes = std::max(1, static_cast<int>(std::ceil(width)));
  std::vector<float> offsets(lanes);
  for (int i = 0; i < lanes; ++i)
    offsets[i] = (i + 0.5f) - lanes * 0.5f;
  EmitRow(effect, ctx, effect.origin, offsets, 1.0f, shift);
}

// Where a burst forms: centred on the aim line just ahead of the caster,
// so it flies exactly where it was aimed
Vector2 BurstCenter(const SpellEffect &effect, float reach) {
  return {effect.origin.x + effect.direction.x * (reach + 1),
          effect.origin.y + effect.direction.y * (reach + 1)};
}

// A burst's pattern all at once
void EmitBurst(SpellEffect &effect, ElementContext &ctx,
               const ShapePart &part) {
  const SpellStats &s = effect.stats;
  if (part.disk) {
    // A round ball: the cells of the smallest disk that holds every
    // particle left, filled from the centre out
    int left = TotalParticles(effect) - effect.emitted;
    int radius = 0;
    std::vector<std::pair<int, int>> cells;
    while (static_cast<int>(cells.size()) < left) {
      cells.clear();
      for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
          if (dx * dx + dy * dy <= radius * radius + radius)
            cells.push_back({dx, dy});
      ++radius;
    }
    std::stable_sort(cells.begin(), cells.end(), [](auto a, auto b) {
      return a.first * a.first + a.second * a.second <
             b.first * b.first + b.second * b.second;
    });
    Vector2 center = BurstCenter(effect, static_cast<float>(radius));
    Vector2 vel{effect.direction.x * s.speed, effect.direction.y * s.speed};
    for (int i = 0; i < left; ++i)
      SpawnSpellParticle(effect, ctx,
                         {center.x + cells[i].first + 0.5f,
                          center.y + cells[i].second + 0.5f},
                         vel);
    return;
  }
  // Art: the front row furthest along the aim
  float scale = SpellShapes::PartScale(part, s.diameter);
  int rows = SpellShapes::ScaledRows(part, scale);
  for (int row = 0; row < rows; ++row)
    EmitRow(effect, ctx, effect.origin,
            SpellShapes::RowOffsets(part, row, scale),
            1.0f, 0.0f, (rows - 1 - row) * part.rowSpacing);
}

// One tick of an element spell, played from its ShapeDef. Rows parts lay
// their rows one cell apart however fast the spell flies (several a tick
// when it's fast), and every particle flies straight on, so a weaving
// emitter draws a snake.
void EmitElement(SpellEffect &effect, ElementContext &ctx, float dt) {
  const SpellStats &s = effect.stats;
  const ShapeDef &def = SpellShapes::Get(s.shape);
  int total = TotalParticles(effect);
  // Rows this tick: the ground the spell covers, a row per cell
  float step = s.speed * dt;
  int perTick = std::max(1, static_cast<int>(std::lround(step)));
  int done = 0; // rows emitted this tick
  while (effect.shapePart < def.parts.size() && effect.emitted < total) {
    const ShapePart &part = def.parts[effect.shapePart];
    if (part.kind == ShapePart::Kind::Burst) {
      if (done > 0)
        return; // a burst gets a tick of its own
      EmitBurst(effect, ctx, part);
      effect.shapePart++;
      return;
    }
    if (part.beamLanes) {
      if (done == 0)
        EmitLanes(effect, ctx, s.diameter, 0.0f);
      return;
    }

    float scale = SpellShapes::PartScale(part, s.diameter);
    int length = SpellShapes::PartLength(part, scale);
    int cycle = SpellShapes::ScaledRows(part, scale);
    while (done < perTick && effect.emitted < total &&
           (length == 0 || effect.partRows < length)) {
      float weave = 0.0f;
      if (def.weaveAmplitude > 0.0f) {
        float phase = 2.0f * PI * effect.rows / def.weaveWavelength;
        weave = def.weaveAmplitude * std::sin(phase);
      }
      // The tick's first row is furthest along: it left a moment earlier
      float ahead = step * (perTick - 1 - done) / perTick;
      EmitRow(effect, ctx, effect.origin,
              SpellShapes::RowOffsets(part, effect.partRows % cycle, scale),
              1.0f, weave, ahead);
      effect.partRows++;
      effect.rows++;
      done++;
    }
    if (length > 0 && effect.partRows >= length) {
      effect.shapePart++; // the next part carries on in this same tick
      effect.partRows = 0;
      continue;
    }
    return;
  }
}

// Column: the figure as (ahead, lateral) points before it is fitted into
// the block. A plain stream fills the block.
std::vector<Vector2> FigurePoints(const SpellStats &s, int count) {
  std::vector<Vector2> pts;
  const ShapeDef &def = SpellShapes::Get(s.shape);
  float row = 0.0f; // how far back the next row sits
  for (const ShapePart &part : def.parts) {
    if (static_cast<int>(pts.size()) >= count)
      break;
    if (part.kind == ShapePart::Kind::Burst && part.disk) {
      int left = count - static_cast<int>(pts.size());
      int r = static_cast<int>(std::ceil(std::sqrt(left / PI))) + 1;
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
          if (dx * dx + dy * dy <= r * r + r)
            pts.push_back({-(row + r + dy), static_cast<float>(dx)});
      row += 2 * r + 1;
      continue;
    }
    float scale = SpellShapes::PartScale(part, s.diameter);
    int cycle = SpellShapes::ScaledRows(part, scale);
    if (part.kind == ShapePart::Kind::Burst) {
      for (int r = 0; r < cycle; ++r)
        for (float off : SpellShapes::RowOffsets(part, r, scale))
          pts.push_back({-(row + r * part.rowSpacing), off});
      row += cycle * part.rowSpacing;
      continue;
    }
    if (part.beamLanes)
      return {}; // a stream: fill the block
    int length = SpellShapes::PartLength(part, scale);
    for (int r = 0; (length == 0 || r < length) &&
                    static_cast<int>(pts.size()) < count;
         ++r) {
      float weave = 0.0f;
      if (def.weaveAmplitude > 0.0f)
        weave = def.weaveAmplitude *
                std::sin(2.0f * PI * (row + r) / def.weaveWavelength);
      for (float off : SpellShapes::RowOffsets(part, r % cycle, scale))
        pts.push_back({-(row + r), off + weave});
    }
    row += length;
  }
  return pts;
}

// The block's cells, as (ahead, lateral) from the near end on the aim line:
// the figure shrunk to fit holdLength x holdWidth, the rest discarded
std::vector<std::pair<int, int>> BlockLayout(const SpellStats &s, int count) {
  int len = std::max(1, static_cast<int>(s.holdLength));
  int width = std::max(1, static_cast<int>(s.holdWidth));
  std::vector<std::pair<int, int>> cells;
  std::vector<Vector2> pts = FigurePoints(s, count);
  if (pts.empty()) {
    for (int a = 0; a < len; ++a)
      for (int l = 0; l < width; ++l)
        cells.push_back({a, l - width / 2});
  } else {
    float a0 = 1e9f, a1 = -1e9f, l0 = 1e9f, l1 = -1e9f;
    for (Vector2 p : pts) {
      a0 = std::min(a0, p.x), a1 = std::max(a1, p.x);
      l0 = std::min(l0, p.y), l1 = std::max(l1, p.y);
    }
    float k = std::min({1.0f, len / (a1 - a0 + 1.0f), width / (l1 - l0 + 1.0f)});
    float lc = (l0 + l1) * 0.5f;
    std::vector<uint8_t> seen(static_cast<size_t>(len) * width, 0);
    for (Vector2 p : pts) {
      // Front of the figure at the far end of the block
      int a = std::clamp(static_cast<int>(std::floor((a1 - p.x) * k)), 0, len - 1);
      int l = static_cast<int>(std::floor((p.y - lc) * k + width * 0.5f));
      if (l < 0 || l >= width || seen[a * width + l])
        continue;
      seen[a * width + l] = 1;
      cells.push_back({len - 1 - a, l - width / 2});
    }
  }
  if (static_cast<int>(cells.size()) > count)
    cells.resize(count);
  return cells;
}

bool Holdable(const ElementContext &ctx, const Cell &c) {
  if (c.element == Element::AIR)
    return true;
  const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];
  bool liquid = props.mobile && !props.solid;
  return (props.passable || liquid) && !(c.flags & CELL_HELD);
}

void PutHeld(const SpellEffect &effect, ElementContext &ctx, int x, int y) {
  const SpellStats &s = effect.stats;
  Cell cell = ElementFactory::Create(s.element, ctx.config);
  if (s.temperature > 0.0f)
    cell.temperature = s.temperature;
  cell.temperature += s.temperatureDelta;
  cell.hardness *= s.hardnessScale;
  cell.flags |= CELL_HELD;
  ctx.grid.Get(x, y) = cell;
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex, true);
}

int ToTicks(float seconds, float dt) {
  return std::max(1, static_cast<int>(std::lround(seconds / dt)));
}

void FormBlock(SpellEffect &effect, ElementContext &ctx, float dt) {
  const SpellStats &s = effect.stats;
  Vector2 d = effect.direction;
  Vector2 perp{-d.y, d.x};
  bool flying = s.speed > 0.0f && s.range > 0.0f;
  // Crushing makes no material, so there's nothing to stand still
  if (s.crush != 0.0f && !flying) {
    effect.emitted = TotalParticles(effect);
    effect.holdPhase = SpellEffect::HoldDone;
    return;
  }
  for (auto [a, l] : BlockLayout(s, TotalParticles(effect))) {
    Vector2 pos{effect.origin.x + d.x * (effect.holdGap + a + 0.5f) + perp.x * (l + 0.5f),
                effect.origin.y + d.y * (effect.holdGap + a + 0.5f) + perp.y * (l + 0.5f)};
    int x = static_cast<int>(std::floor(pos.x));
    int y = static_cast<int>(std::floor(pos.y));
    if (!ctx.grid.InBounds(x, y))
      continue;
    if (flying) {
      // A little spare range: the spell sets the block down itself
      if (Particle *p = ctx.particles.Spawn(
              {x + 0.5f, y + 0.5f}, {d.x * s.speed, d.y * s.speed}, s.element,
              s.range + 8.0f, s.power, true, effect.owner)) {
        p->castId = effect.castId;
        p->temperature = s.temperature;
        p->temperatureDelta = s.temperatureDelta;
        p->hardnessScale = s.hardnessScale;
        p->crush = s.crush;
        p->restore = s.restore;
      }
      continue;
    }
    effect.holdCells.push_back(y * GRID_W + x);
    if (Holdable(ctx, ctx.grid.Get(x, y)))
      PutHeld(effect, ctx, x, y);
  }
  effect.emitted = TotalParticles(effect);
  if (flying) {
    effect.holdPhase = SpellEffect::HoldFlying;
    effect.holdTicks = ToTicks(s.range / s.speed, dt);
  } else {
    effect.holdPhase = SpellEffect::HoldHolding;
    effect.holdTicks = ToTicks(s.holdTime, dt);
  }
}

// The block has flown its range: what's left of it is set down and held
void LandBlock(SpellEffect &effect, ElementContext &ctx, float dt) {
  ctx.particles.ForEachActive([&](Particle &p) {
    if (p.castId != effect.castId || !p.isProjectile)
      return;
    p.active = false;
    int x = static_cast<int>(std::floor(p.pos.x));
    int y = static_cast<int>(std::floor(p.pos.y));
    if (p.crush != 0.0f || !ctx.grid.InBounds(x, y) ||
        !Holdable(ctx, ctx.grid.Get(x, y)))
      return;
    effect.holdCells.push_back(y * GRID_W + x);
    PutHeld(effect, ctx, x, y);
  });
  effect.holdPhase = SpellEffect::HoldHolding;
  effect.holdTicks = ToTicks(effect.stats.holdTime, dt);
}

// Keep the block in shape. With repetition it stays as cast (its heat and
// hardness put back) and mends holes; without, damage stays.
void HoldBlock(SpellEffect &effect, ElementContext &ctx) {
  const SpellStats &s = effect.stats;
  int mends = s.restore > 0.0f
                  ? std::max(1, static_cast<int>(s.restore * kTuning.mendsPerSign))
                  : 0;
  for (int32_t i : effect.holdCells) {
    int x = i % GRID_W, y = i / GRID_W;
    Cell &c = ctx.grid.Get(x, y);
    bool ours = c.element == s.element && (c.flags & CELL_HELD);
    if (ours && s.restore > 0.0f) {
      const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];
      c.temperature = s.temperature > 0.0f ? s.temperature
                                           : props.defaultTemperature;
      c.hardness = props.defaultHardness * s.hardnessScale;
      c.flags &= ~CELL_BURNING;
    } else if (!ours && mends > 0 && Holdable(ctx, c)) {
      PutHeld(effect, ctx, x, y);
      --mends;
    }
  }
}

void ReleaseBlock(SpellEffect &effect, ElementContext &ctx) {
  for (int32_t i : effect.holdCells) {
    int x = i % GRID_W, y = i / GRID_W;
    Cell &c = ctx.grid.Get(x, y);
    if (c.flags & CELL_HELD) {
      c.flags &= ~CELL_HELD;
      ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex, true);
    }
  }
  effect.holdCells.clear();
  effect.holdPhase = SpellEffect::HoldDone;
}

void TickColumn(SpellEffect &effect, ElementContext &ctx, float dt) {
  switch (effect.holdPhase) {
  case SpellEffect::HoldForming:
    FormBlock(effect, ctx, dt);
    break;
  case SpellEffect::HoldFlying:
    if (--effect.holdTicks <= 0)
      LandBlock(effect, ctx, dt);
    break;
  case SpellEffect::HoldHolding:
    HoldBlock(effect, ctx);
    if (--effect.holdTicks <= 0)
      ReleaseBlock(effect, ctx);
    break;
  default:
    break;
  }
}

// Move what the field holds: acceleration = force * strength / mass, toward
// the caster when pulling, away when pushing. A wind field moves every loose
// thing, an element's field only that element.
void ApplyField(SpellEffect &field, ElementContext &ctx,
                RigidBodySystem &bodies, float dt) {
  const SpellStats &s = field.stats;
  Vector2 d = SpellSystem::FieldPush(field);

  // Loose particles, including other spells' projectiles
  ctx.particles.ForEachActive([&](Particle &p) {
    if ((p.owner == field.owner && p.isProjectile) ||
        !SpellSystem::FieldMoves(s, p.element))
      return;
    float strength = SpellSystem::FieldStrengthAt(field, p.pos);
    if (strength <= 0.0f)
      return;
    float accel = s.force * strength / ElementMass(ctx.config, p.element);
    p.vel.x += d.x * accel * dt;
    p.vel.y += d.y * accel * dt;
  });

  // Grid cells: loose material is picked up into particles, rigid bodies get
  // an impulse for the cells of theirs the field covers, terrain stays put
  struct BodyPush {
    int cells = 0;
    Vector2 sum{0.0f, 0.0f};
    float mass = 0.0f;
  };
  std::unordered_map<int32_t, BodyPush> bodyPushes;

  Vector2 o = field.origin;
  Vector2 end{o.x + field.direction.x * s.range,
              o.y + field.direction.y * s.range};
  int x0 = (int)std::floor(std::min(o.x, end.x) - s.diameter);
  int x1 = (int)std::ceil(std::max(o.x, end.x) + s.diameter);
  int y0 = (int)std::floor(std::min(o.y, end.y) - s.diameter);
  int y1 = (int)std::ceil(std::max(o.y, end.y) + s.diameter);

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (!ctx.grid.InBounds(x, y))
        continue;
      Cell &c = ctx.grid.Get(x, y);
      if (c.element == Element::AIR || !SpellSystem::FieldMoves(s, c.element))
        continue;
      float strength =
          SpellSystem::FieldStrengthAt(field, {x + 0.5f, y + 0.5f});
      if (strength <= 0.0f)
        continue;
      float mass = ElementMass(ctx.config, c.element);
      float accel = s.force * strength / mass;

      if (c.bodyID >= 0) {
        BodyPush &push = bodyPushes[c.bodyID];
        push.cells++;
        push.sum.x += x + 0.5f;
        push.sum.y += y + 0.5f;
        push.mass += mass;
        continue;
      }

      const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];
      bool loose = props.mobile || c.element == Element::FIRE;
      if (!loose || props.staticTerrain || accel < kTuning.gustLiftAccel)
        continue;
      if ((ctx.rng() % 1000) / 1000.0f >= kTuning.gustLiftChance)
        continue;

      Vector2 vel{d.x * accel * kTuning.gustLiftVelocity,
                  d.y * accel * kTuning.gustLiftVelocity};
      if (Particle *p = ctx.particles.Spawn({x + 0.5f, y + 0.5f}, vel,
                                            c.element)) {
        p->temperature = c.temperature;
        c = ElementFactory::Create(Element::AIR, ctx.config);
        ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
      }
    }
  }

  for (const auto &[bodyId, push] : bodyPushes) {
    // impulse = mass * acceleration * dt over the covered cells
    float accel = s.force / (push.mass / push.cells);
    Vector2 impulse{d.x * accel * push.mass * dt, d.y * accel * push.mass * dt};
    Vector2 point{push.sum.x / push.cells, push.sum.y / push.cells};
    bodies.ApplyImpulse(bodyId, impulse, point);
  }
}

} // namespace

void SpellSystem::TickEffects(std::vector<SpellEffect> &effects,
                              ElementContext &ctx, RigidBodySystem &bodies,
                              float dt) {
  for (auto &effect : effects) {
    switch (effect.stats.kind) {
    case SpellKind::Element:
      if (effect.stats.holdTime > 0.0f)
        TickColumn(effect, ctx, dt);
      else
        EmitElement(effect, ctx, dt);
      break;
    case SpellKind::Field:
      ApplyField(effect, ctx, bodies, dt);
      effect.timeRemaining -= dt;
      break;
    default:
      break;
    }
  }

  std::erase_if(effects, [](const SpellEffect &effect) {
    switch (effect.stats.kind) {
    case SpellKind::Element:
      if (effect.stats.holdTime > 0.0f)
        return effect.holdPhase == SpellEffect::HoldDone;
      return effect.emitted >= TotalParticles(effect) ||
             effect.shapePart >= SpellShapes::Get(effect.stats.shape).parts.size();
    case SpellKind::Field:
      return effect.timeRemaining <= 0.0f;
    default:
      return true;
    }
  });
}
