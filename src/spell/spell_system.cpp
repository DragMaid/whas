#include "whas/spell/spell_system.h"
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
  float maxLaunchSpeed = 120.0f;

  // Gust: force = speed * (0.5 + sigil) * this. Four balanced signs with a
  // full-size sigil give ~90: rock (mass 3) is shoved to ~20 cells/s over the
  // gust, water (mass 1) to ~60, a character (mass 3) like the rock.
  float gustForcePerSpeed = 0.6f;
  float gustBaseDuration = 0.35f;
  float gustDurationPerSigil = 0.3f;
  float gustWidthScale = 1.5f; // field width relative to the beam diameter
  float gustLiftAccel = 40.0f; // cells need this much push to be picked up
  float gustLiftChance = 0.25f;
  float gustLiftVelocity = 0.1f; // lifted cell starts at accel * this

  // Modifier signs; each scales with the summed scale of its signs
  float convergenceDensity = 0.5f; // denser...
  float convergenceNarrow = 0.4f;  // ...narrower...
  float convergenceHardness = 0.25f; // ...and a little harder when it lands
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
  float aboveHeadGap = 4.0f; // cells from the caster's centre to an orb
  // Layered spells: an embedded spell of scale s is worth s / this
  float componentFullScale = 0.4f;
  float componentMinEffect = 0.5f;
  float componentMaxEffect = 1.25f;

};

constexpr SpellTuning kTuning;

float Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }

} // namespace

SpellKind SpellSystem::SigilKind(const std::string &assetId) {
  if (assetId == "wind")
    return SpellKind::Flight;
  if (assetId == "gust")
    return SpellKind::Gust;
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
  default:
    return 1.0f;
  }
}

bool SpellSystem::IsShapeSigil(const std::string &assetId) {
  return SpellShapes::TriggerFor(assetId, GlyphKind::Sigil) != nullptr;
}

bool SpellSystem::SignInvertible(const std::string &assetId) {
  return assetId == "crushing" || assetId == "expansion";
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
  // Summed scales of each shape's trigger glyphs
  std::array<float, static_cast<size_t>(SpellShape::Count)> shapes{};

  Modifiers operator+(const Modifiers &o) const {
    Modifiers m{convergence + o.convergence, crush + o.crush,
                repetition + o.repetition,   cooling + o.cooling,
                strengthening + o.strengthening, collection + o.collection,
                expansion + o.expansion,     {}};
    for (size_t i = 0; i < shapes.size(); ++i)
      m.shapes[i] = shapes[i] + o.shapes[i];
    return m;
  }
};

// What one circle's glyphs add up to
struct Circle {
  int sigilCount = 0; // sigils that pick the kind
  int shapeSigils = 0;
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
  for (const auto &glyph : glyphs) {
    const std::string &id = glyph.assetId;
    if (const ShapeTrigger *t = SpellShapes::TriggerFor(id, glyph.kind)) {
      c.mods.shapes[static_cast<size_t>(t->shape)] += glyph.scale;
      if (glyph.kind == GlyphKind::Sigil)
        c.shapeSigils++;
      continue;
    }
    if (glyph.kind == GlyphKind::Sigil) {
      c.sigilCount++;
      c.sigilScale = glyph.scale;
      c.kind = SpellSystem::SigilKind(id);
      c.element = SpellSystem::SigilElement(id);
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
    else {
      // Column: sign glyphs point up in their SVG; rotate like
      // SpellGeometry does
      float rad = glyph.rotationDeg * DEG2RAD;
      Vector2 forward{std::sin(rad), -std::cos(rad)};
      c.net.x += forward.x * glyph.scale;
      c.net.y += forward.y * glyph.scale;
      c.magnitude += glyph.scale;
      c.thrustSigns++;
    }
  }
  return c;
}

// How a circle's column signs steer and speed up the spell
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
            c.shapeSigils <= 1;
  s.kind = c.kind;
  s.element = c.element;
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

    if (mods.convergence > 0.0f) {
      s.density *= 1.0f + kTuning.convergenceDensity * mods.convergence;
      s.diameter /= 1.0f + kTuning.convergenceNarrow * mods.convergence;
      s.hardnessScale *= 1.0f + kTuning.convergenceHardness * mods.convergence;
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

    s.power = 0.5f * s.density * s.speed * s.speed * kTuning.powerScale;
    s.particleCount = std::clamp(static_cast<int>(count * effect), 1,
                                 kTuning.maxParticles);
    break;
  }
  case SpellKind::Flight:
    s.launchSpeed = std::min(
        kTuning.maxLaunchSpeed,
        s.speed * (kTuning.launchBase + kTuning.launchPerSigil * sigilScale) *
            effect);
    break;
  case SpellKind::Gust:
    s.force =
        s.speed * (0.5f + sigilScale) * kTuning.gustForcePerSpeed * effect;
    s.duration =
        kTuning.gustBaseDuration + kTuning.gustDurationPerSigil * sigilScale;
    s.diameter = Expand(s.diameter * kTuning.gustWidthScale, mods.expansion);
    break;
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
  s.valid = outer.sigilCount == 0 && outer.shapeSigils == 0 && count >= 1 &&
            count <= LAYER_MAX_COMPONENTS;
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

float SpellSystem::GustStrengthAt(const SpellEffect &gust, Vector2 point) {
  Vector2 rel{point.x - gust.origin.x, point.y - gust.origin.y};
  float along = Dot(rel, gust.direction);
  if (along < 0.0f || along > gust.stats.range)
    return 0.0f;
  float across = std::abs(rel.x * gust.direction.y - rel.y * gust.direction.x);
  if (across > gust.stats.diameter * 0.5f)
    return 0.0f;
  return 1.0f - 0.5f * (along / gust.stats.range);
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

void SpawnSpellParticle(SpellEffect &effect, ElementContext &ctx, Vector2 pos,
                        Vector2 vel) {
  const SpellStats &s = effect.stats;
  float range = s.range + FigureExtraRange(effect);
  if (Particle *p = ctx.particles.Spawn(pos, vel, s.element, range, s.power,
                                        true, effect.owner)) {
    p->temperature = s.temperature;
    p->temperatureDelta = s.temperatureDelta;
    p->hardnessScale = s.hardnessScale;
    p->crush = s.crush;
    p->restore = s.restore;
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

// Where a burst forms: just ahead of the caster, or over their head
Vector2 BurstCenter(const SpellEffect &effect, const ShapePart &part,
                    float reach) {
  if (part.anchor == ShapePart::Anchor::Above)
    return {effect.origin.x,
            effect.origin.y - reach - kTuning.aboveHeadGap};
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
    Vector2 center = BurstCenter(effect, part, static_cast<float>(radius));
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
  Vector2 base = part.anchor == ShapePart::Anchor::Above
                     ? BurstCenter(effect, part, 0.0f)
                     : effect.origin;
  for (int row = 0; row < rows; ++row)
    EmitRow(effect, ctx, base, SpellShapes::RowOffsets(part, row, scale),
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

// Push everything in the field: acceleration = force * strength / mass
void ApplyGust(SpellEffect &gust, ElementContext &ctx, RigidBodySystem &bodies,
               float dt) {
  const SpellStats &s = gust.stats;
  Vector2 d = gust.direction;

  // Loose particles, including other spells' projectiles
  ctx.particles.ForEachActive([&](Particle &p) {
    if (p.owner == gust.owner && p.isProjectile)
      return;
    float strength = SpellSystem::GustStrengthAt(gust, p.pos);
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

  int x0 = (int)std::floor(std::min(gust.origin.x, gust.origin.x + d.x * s.range) - s.diameter);
  int x1 = (int)std::ceil(std::max(gust.origin.x, gust.origin.x + d.x * s.range) + s.diameter);
  int y0 = (int)std::floor(std::min(gust.origin.y, gust.origin.y + d.y * s.range) - s.diameter);
  int y1 = (int)std::ceil(std::max(gust.origin.y, gust.origin.y + d.y * s.range) + s.diameter);

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (!ctx.grid.InBounds(x, y))
        continue;
      Cell &c = ctx.grid.Get(x, y);
      if (c.element == Element::AIR)
        continue;
      float strength = SpellSystem::GustStrengthAt(gust, {x + 0.5f, y + 0.5f});
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
      EmitElement(effect, ctx, dt);
      break;
    case SpellKind::Gust:
      ApplyGust(effect, ctx, bodies, dt);
      effect.timeRemaining -= dt;
      break;
    default:
      break;
    }
  }

  std::erase_if(effects, [](const SpellEffect &effect) {
    switch (effect.stats.kind) {
    case SpellKind::Element:
      return effect.emitted >= TotalParticles(effect) ||
             effect.shapePart >= SpellShapes::Get(effect.stats.shape).parts.size();
    case SpellKind::Gust:
      return effect.timeRemaining <= 0.0f;
    default:
      return true;
    }
  });
}
