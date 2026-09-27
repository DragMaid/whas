#include "whas/spell/spell_system.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/particle_system.h"
#include "whas/physics/rigid_body_system.h"
#include "whas/world/grid.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

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

SpellStats SpellSystem::Evaluate(const Spell &spell) {
  SpellStats s;

  int sigilCount = 0;
  int signCount = 0;
  float sigilScale = 0.0f;

  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind == GlyphKind::Sigil) {
      sigilCount++;
      sigilScale = glyph.scale;
      s.kind = SigilKind(glyph.assetId);
      s.element = SigilElement(glyph.assetId);
      continue;
    }

    // Sign glyphs point up in their SVG; rotate like SpellGeometry does
    float rad = glyph.rotationDeg * DEG2RAD;
    Vector2 forward{std::sin(rad), -std::cos(rad)};
    s.netLocal.x += forward.x * glyph.scale;
    s.netLocal.y += forward.y * glyph.scale;
    s.totalMagnitude += glyph.scale;
    signCount++;
  }

  s.valid = sigilCount == 1 && s.kind != SpellKind::None;

  float lateral = s.netLocal.x;
  float forward = -s.netLocal.y;
  float lateralRatio = 0.0f;
  if (s.totalMagnitude > 0.0f) {
    s.imbalance = std::min(
        1.0f, std::hypot(s.netLocal.x, s.netLocal.y) / s.totalMagnitude);
    lateralRatio = std::min(1.0f, std::abs(lateral) / s.totalMagnitude);
    float maxOffset = kTuning.maxOffsetDeg * DEG2RAD;
    s.offsetRad = std::clamp(
        std::atan2(lateral, std::max(0.0f, s.totalMagnitude + forward)),
        -maxOffset, maxOffset);
  }

  s.speed = kTuning.baseSpeed +
            kTuning.speedPerSign * s.totalMagnitude *
                (1.0f - kTuning.lateralSpeedLoss * lateralRatio);
  s.range = s.speed * kTuning.flightTime;

  s.diameter = std::clamp(
      kTuning.baseDiameter + sigilScale * kTuning.diameterPerSigilScale, 1.0f,
      kTuning.maxDiameter);

  switch (s.kind) {
  case SpellKind::Element: {
    s.density = SpellDensity(s.element) *
                (kTuning.densityBase + kTuning.densityPerSigil * sigilScale);
    s.power = 0.5f * s.density * s.speed * s.speed * kTuning.powerScale;
    int count = kTuning.baseParticles +
                static_cast<int>(sigilScale * kTuning.particlesPerSigilScale) +
                signCount * kTuning.particlesPerSign;
    s.particleCount = std::clamp(count, 1, kTuning.maxParticles);
    if (s.element == Element::FIRE)
      s.temperature =
          kTuning.fireBaseTemp + kTuning.fireTempPerSigil * sigilScale;
    break;
  }
  case SpellKind::Flight:
    s.launchSpeed = std::min(
        kTuning.maxLaunchSpeed,
        s.speed * (kTuning.launchBase + kTuning.launchPerSigil * sigilScale));
    break;
  case SpellKind::Gust:
    s.force = s.speed * (0.5f + sigilScale) * kTuning.gustForcePerSpeed;
    s.duration =
        kTuning.gustBaseDuration + kTuning.gustDurationPerSigil * sigilScale;
    s.diameter *= kTuning.gustWidthScale;
    break;
  case SpellKind::None:
    break;
  }

  return s;
}

Vector2 SpellSystem::ResolveDirection(const SpellStats &stats, Vector2 aim) {
  float c = std::cos(stats.offsetRad);
  float s = std::sin(stats.offsetRad);
  return {aim.x * c - aim.y * s, aim.x * s + aim.y * c};
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

void EmitElementStream(SpellEffect &effect, ElementContext &ctx) {
  const SpellStats &s = effect.stats;
  int lanes = std::max(1, static_cast<int>(std::ceil(s.diameter)));
  Vector2 perp{-effect.direction.y, effect.direction.x};
  Vector2 vel{effect.direction.x * s.speed, effect.direction.y * s.speed};

  for (int i = 0; i < lanes && effect.emitted < s.particleCount; ++i) {
    float lateral = (i + 0.5f) - lanes * 0.5f;
    Vector2 pos{effect.origin.x + perp.x * lateral,
                effect.origin.y + perp.y * lateral};
    if (Particle *p = ctx.particles.Spawn(pos, vel, s.element, s.range,
                                          s.power, true, effect.owner))
      p->temperature = s.temperature;
    effect.emitted++;
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
      EmitElementStream(effect, ctx);
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
      return effect.emitted >= effect.stats.particleCount;
    case SpellKind::Gust:
      return effect.timeRemaining <= 0.0f;
    default:
      return true;
    }
  });
}
