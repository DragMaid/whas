#include "whas/physics/particle_system.h"
#include "whas/constants.h"
#include "whas/core/config.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/spell/spell_system.h"
#include "whas/world/grid.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

ParticleSystem::ParticleSystem(int maxParticles)
    : m_maxParticles(maxParticles) {
  m_particles.resize(maxParticles);
}

void ParticleSystem::SpawnFrom(ElementContext &ctx, Vector2 pos, Vector2 vel,
                               Element element) {
  if (ctx.deferredSpawns)
    ctx.deferredSpawns->push_back({pos, vel, element});
  else
    ctx.particles.Spawn(pos, vel, element);
}

Particle *ParticleSystem::Spawn(Vector2 pos, Vector2 vel, Element element,
                                float remainingDistance, float power,
                                bool isProjectile, int owner) {
  for (auto &p : m_particles) {
    if (!p.active) {
      p.pos = pos;
      p.vel = vel;
      p.element = element;
      p.active = true;
      p.isProjectile = isProjectile;
      p.remainingDistance = remainingDistance;
      p.power = power;
      p.owner = owner;
      p.temperatureDelta = 0.0f;
      p.hardnessScale = 1.0f;
      p.crush = 0.0f;
      p.restore = 0.0f;
      p.temperature = 0.0f;
      p.flashRadius = 0.0f;
      p.flashTime = 0.0f;
      p.guideId = -1;
      p.pathS = 0.0f;
      p.pathL = 0.0f;
      p.castId = -1;
      p.lastHit = -1;
      return &p;
    }
  }
  return nullptr;
}

namespace {

// Loose rock would immediately be re-extracted and crumbled by the rigid body
// system, so it settles as rubble instead.
Element DepositedElement(Element element) {
  return element; // PROBE
}

// How impacts spend a projectile's power
struct ImpactTuning {
  float granularCostScale = 0.3f; // shoving loose grains is cheaper than breaking
  float granularSlowdown = 0.85f;
  float solidSlowdown = 0.6f;
  float ejectaSpeedScale = 0.25f; // debris kicked back at this share of speed
  float ejectaSpreadDeg = 50.0f;
  float ejectaLift = 8.0f;
};

constexpr ImpactTuning kImpact;

// Broken solids leave loose rubble rather than floating chunks
Element RubbleOf(Element element) {
  switch (element) {
  case Element::EARTH:
  case Element::ROCK:
    return Element::SAND;
  default:
    return element;
  }
}

float RandomUnit(ElementContext &ctx) {
  return (ctx.rng() % 10001) / 10000.0f;
}

// A projectile hitting a solid cell. Loose grains (sand) get shoved along as
// particles; rigid material (earth, rock, ice) breaks into rubble that is
// kicked back out of the hole. Either way the projectile pays for it in power
// and speed. Returns false when the projectile can't get through.
bool TryImpact(Particle &p, Grid &grid, ElementContext &ctx, int tx, int ty) {
  Cell &target = grid.Get(tx, ty);
  const auto &props = ctx.config.elements[static_cast<size_t>(target.element)];
  bool granular = props.mobile && props.solid;
  float cost = granular ? target.hardness * kImpact.granularCostScale
                        : target.hardness;
  if (p.power < cost)
    return false;
  p.power -= cost;

  Vector2 center{tx + 0.5f, ty + 0.5f};
  Vector2 debrisVel;
  if (granular) {
    // Momentum shared by mass: light projectiles barely move heavy grains
    float pm = ElementMass(ctx.config, p.element);
    float gm = ElementMass(ctx.config, target.element);
    float share = pm / (pm + gm);
    debrisVel = {p.vel.x * share, p.vel.y * share - RandomUnit(ctx) * 3.0f};
    p.vel.x *= kImpact.granularSlowdown;
    p.vel.y *= kImpact.granularSlowdown;
  } else {
    float spread = (RandomUnit(ctx) * 2.0f - 1.0f) * kImpact.ejectaSpreadDeg *
                   DEG2RAD;
    float c = std::cos(spread), s = std::sin(spread);
    Vector2 back{-p.vel.x * kImpact.ejectaSpeedScale,
                 -p.vel.y * kImpact.ejectaSpeedScale};
    debrisVel = {back.x * c - back.y * s,
                 back.x * s + back.y * c - kImpact.ejectaLift};
    p.vel.x *= kImpact.solidSlowdown;
    p.vel.y *= kImpact.solidSlowdown;
  }

  if (!granular)
    ctx.particles.Note({ParticleNoise::Break, target.element, center});
  Element debris = granular ? target.element : RubbleOf(target.element);
  float temperature = target.temperature;
  bool wasStatic = IsStaticCell(target, props);
  target = ElementFactory::Create(Element::AIR, ctx.config);
  ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex, wasStatic);
  if (Particle *d = ctx.particles.Spawn(center, debrisVel, debris))
    d->temperature = temperature;
  return true;
}

// Turn the particle back into a grid cell at the nearest free spot, searching
// outward ring by ring (upper cells first) so displaced material piles up
// instead of vanishing when it lands somewhere crowded
void Deposit(Particle &p, Grid &grid, ElementContext &ctx) {
  constexpr int kMaxRadius = 8;
  p.active = false;
  if (p.element == Element::AIR)
    return;
  if (p.castId >= 0)
    ctx.particles.Note({ParticleNoise::Impact, p.element, p.pos});

  int px = static_cast<int>(std::floor(p.pos.x));
  int py = static_cast<int>(std::floor(p.pos.y));

  for (int r = 0; r <= kMaxRadius; ++r) {
    for (int dy = -r; dy <= r; ++dy) {
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r)
          continue; // ring only
        int x = px + dx;
        int y = py + dy;
        if (!grid.InBounds(x, y) || grid.Get(x, y).element != Element::AIR)
          continue;

        Element element = p.element;
        Cell cell = ElementFactory::Create(element, ctx.config);
        if (p.temperature > 0.0f)
          cell.temperature = p.temperature;
        cell.temperature += p.temperatureDelta;
        cell.hardness *= p.hardnessScale;
        if (element == Element::FIRE && p.temperature > 0.0f) {
          // Hotter fire carries more fuel and burns longer
          const auto &fire = ctx.config.elements[static_cast<size_t>(element)];
          float scale = std::clamp(p.temperature / fire.defaultTemperature,
                                   1.0f, ctx.config.fire.maxFuelScale);
          cell.lifetime *= scale;
        }
        grid.Get(x, y) = cell;
        const auto &props = ctx.config.elements[static_cast<size_t>(element)];
        ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex, props.staticTerrain);
        return;
      }
    }
  }
}

// How crushing and repetition reach into what a projectile hits
struct ModifierTuning {
  float crushChancePerSign = 0.35f; // per cell, per hit
  float reformChancePerSign = 0.25f;
  float radiusPerSign = 1.0f; // cells beyond the one that was hit
  int maxRadius = 4;
  // Crushed grit is thrown out of the hole, away from where it was hit
  float debrisSpeedMin = 25.0f; // cells/s
  float debrisSpeedMax = 60.0f;
  float debrisLift = 15.0f;
};

constexpr ModifierTuning kModifier;

void ReplaceCell(Grid &grid, ElementContext &ctx, int x, int y,
                 Element element) {
  Cell &c = grid.Get(x, y);
  bool wasStatic =
      IsStaticCell(c, ctx.config.elements[static_cast<size_t>(c.element)]);
  float temperature = c.temperature;
  c = ElementFactory::Create(element, ctx.config);
  c.temperature = temperature;
  bool isStatic =
      ctx.config.elements[static_cast<size_t>(element)].staticTerrain;
  ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex, wasStatic || isStatic);
}

// Crushing grinds rock and earth into sand thrown out of the hole; inverted, it packs sand back into
// earth. Repetition puts cells back the way the world made them: default
// temperature and hardness, no longer burning.
void ApplyHitModifiers(const Particle &p, Grid &grid, ElementContext &ctx,
                       int tx, int ty) {
  float strength = std::max(std::abs(p.crush), p.restore);
  if (strength <= 0.0f)
    return;
  int r = std::min(kModifier.maxRadius,
                   static_cast<int>(strength * kModifier.radiusPerSign));
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      int x = tx + dx, y = ty + dy;
      if (dx * dx + dy * dy > r * r || !grid.InBounds(x, y))
        continue;
      Cell &c = grid.Get(x, y);
      if (p.crush > 0.0f &&
          (c.element == Element::ROCK || c.element == Element::EARTH) &&
          RandomUnit(ctx) < p.crush * kModifier.crushChancePerSign) {
        ctx.particles.Note({ParticleNoise::Break, c.element, {x + 0.5f, y + 0.5f}});
        ReplaceCell(grid, ctx, x, y, Element::AIR);
        // Out of the hole: away from the hit, or back along the spell
        Vector2 out{static_cast<float>(dx), static_cast<float>(dy)};
        if (dx == 0 && dy == 0)
          out = {-p.vel.x, -p.vel.y};
        float len = std::sqrt(out.x * out.x + out.y * out.y);
        if (len > 0.0f)
          out = {out.x / len, out.y / len};
        float speed = kModifier.debrisSpeedMin +
                      RandomUnit(ctx) * (kModifier.debrisSpeedMax -
                                         kModifier.debrisSpeedMin);
        ParticleSystem::SpawnFrom(
            ctx, {x + 0.5f, y + 0.5f},
            {out.x * speed, out.y * speed - kModifier.debrisLift},
            Element::SAND);
      } else if (p.crush < 0.0f && c.element == Element::SAND &&
                 RandomUnit(ctx) < -p.crush * kModifier.reformChancePerSign) {
        ReplaceCell(grid, ctx, x, y, Element::EARTH);
      }
      if (p.restore > 0.0f && c.element != Element::AIR) {
        const auto &props =
            ctx.config.elements[static_cast<size_t>(c.element)];
        c.temperature = props.defaultTemperature;
        c.hardness = props.defaultHardness;
        c.flags &= ~CELL_BURNING;
        ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
      }
    }
  }
}

// What an element does when a spell runs into it (or it into a spell).
// Light and gases have no body: things pass straight through them.
enum class Body { None, Flame, Liquid, Solid };

Body BodyOf(Element e) {
  switch (e) {
  case Element::AIR:
  case Element::LIGHT:
  case Element::STEAM:
  case Element::SMOKE:
  case Element::CLOUD:
    return Body::None;
  case Element::FIRE:
    return Body::Flame;
  case Element::WATER:
    return Body::Liquid;
  default:
    return Body::Solid;
  }
}

// How spells meet each other and the world
struct ClashTuning {
  float splashSpeed = 0.45f;   // splashed share of what hit it
  float splashSpreadDeg = 70.0f;
  float splashLift = 8.0f;      // cells/s upward
  float quenchChance = 0.2f;    // water spent (to steam) meeting a fireball
  float douseSpendChance = 0.06f; // ...and per burning cell it douses
  float quenchedSpeed = 0.9f;   // water after putting out fire keeps this
  float steamLift = 6.0f;
  float throughLiquid = 0.96f;  // a solid keeps this much speed per cell
  float throughFlame = 0.99f;   // of water or fire it ploughs through
  float restitution = 0.2f;     // solid on solid
  float dropSpeed = 12.0f;      // slower than this, a spell lets go
  float boilChance = 0.3f; // a fireball hitting water boils that cell
  int douseRadius = 2;     // water meeting fire or landing puts out this far
  float throughFire = 0.8f; // water keeps this much speed per fire cell: it
                            // drops into a blaze rather than streak through
  float absorbRatio = 2.0f; // water this many times the other's mass takes
                            // its drops in instead of both splashing
};

constexpr ClashTuning kClash;

// Scale a projectile's speed (and its power with the square)
void Slow(Particle &p, float factor) {
  p.vel.x *= factor;
  p.vel.y *= factor;
  p.power *= factor * factor;
}

// No longer flying: it falls (or rises) with the world from here on
void LetGo(Particle &p) {
  p.isProjectile = false;
  p.power = 0.0f;
  p.guideId = -1;
}

// Knocked aside by something moving at `push`: sprays off at an angle
void Splash(Particle &p, Vector2 push, ElementContext &ctx) {
  LetGo(p);
  float spread =
      (RandomUnit(ctx) * 2.0f - 1.0f) * kClash.splashSpreadDeg * DEG2RAD;
  float c = std::cos(spread), s = std::sin(spread);
  Vector2 v{push.x * kClash.splashSpeed, push.y * kClash.splashSpeed};
  p.vel = {v.x * c - v.y * s, v.x * s + v.y * c - kClash.splashLift};
}

// Fire put out: a puff of steam
void Quench(Particle &p) {
  LetGo(p);
  p.element = Element::STEAM;
  p.temperature = 0.0f;
  p.vel = {p.vel.x * 0.1f, -kClash.steamLift};
}

// Put-out fire and boiled water are ordinary steam, at steam's own
// temperature (steam as hot as the fire rises many cells a tick)
void MakeSteam(Grid &grid, ElementContext &ctx, int x, int y) {
  ReplaceCell(grid, ctx, x, y, Element::STEAM);
  grid.Get(x, y).temperature =
      ctx.config.elements[static_cast<size_t>(Element::STEAM)]
          .defaultTemperature;
}

// Water puts out fire and burning cells around where it hits: a bolt soaks
// a patch, not just the line each drop flies along
void Douse(Grid &grid, ElementContext &ctx, int cx, int cy) {
  int r = kClash.douseRadius;
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x) {
      if (!grid.InBounds(x, y) ||
          (x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r + r)
        continue;
      Cell &c = grid.Get(x, y);
      if (c.bodyID >= 0)
        continue;
      if (c.element == Element::FIRE) {
        MakeSteam(grid, ctx, x, y);
      } else if (c.flags & CELL_BURNING) {
        c.flags &= ~CELL_BURNING;
        c.temperature = ctx.config.fire.extinguishTemp;
        ctx.chunks.WakeChunkAt(x, y, ctx.frameIndex);
      }
    }
}

// A flying spell particle reaching a cell it doesn't simply break or stop
// at: water puts out fire, fire dies in water, solids plough through water
// and fire and splash them aside. Returns false when the particle stops
// flying here.
bool MeetCell(Particle &p, Grid &grid, ElementContext &ctx, int x, int y) {
  Cell &cell = grid.Get(x, y);
  if (cell.bodyID >= 0)
    return true; // part of a rigid body: the usual impact
  Body self = BodyOf(p.element);
  Body other = BodyOf(cell.element);
  if (self == Body::Liquid &&
      (other == Body::Flame || (cell.flags & CELL_BURNING))) {
    ctx.particles.Note({ParticleNoise::Fizzle, p.element, p.pos});
    Douse(grid, ctx, x, y);
    if (RandomUnit(ctx) < kClash.douseSpendChance) {
      Quench(p); // this drop went up in steam
      return false;
    }
    // Poured into the blaze: slowed, and too slow it falls in and soaks it
    Slow(p, kClash.throughFire);
    if (std::hypot(p.vel.x, p.vel.y) < kClash.dropSpeed) {
      LetGo(p);
      return false;
    }
  } else if (self == Body::Flame && other == Body::Liquid) {
    ctx.particles.Note({ParticleNoise::Fizzle, p.element, p.pos});
    if (RandomUnit(ctx) < kClash.boilChance)
      MakeSteam(grid, ctx, x, y);
    Quench(p);
    return false;
  } else if (self == Body::Solid &&
             (other == Body::Liquid || other == Body::Flame)) {
    // Splash it out of the way and keep going, a little slower
    Element splashed = cell.element;
    float temperature = cell.temperature;
    ReplaceCell(grid, ctx, x, y, Element::AIR);
    if (Particle *d = ctx.particles.Spawn({x + 0.5f, y + 0.5f}, {0, 0},
                                          splashed)) {
      d->temperature = temperature;
      Splash(*d, p.vel, ctx);
    }
    Slow(p, other == Body::Liquid ? kClash.throughLiquid
                                  : kClash.throughFlame);
  }
  return true;
}

} // namespace

namespace {

// How far ahead along its path a particle aims (cells): enough to round a
// turn smoothly, little enough to stay on the path
constexpr float kLookAhead = 3.0f;

float WrapAngle(float a) {
  while (a > PI)
    a -= 2.0f * PI;
  while (a < -PI)
    a += 2.0f * PI;
  return a;
}

// A point on a guide's path `s` cells from the cast, and which way the path
// runs there. Before the start and past the end it carries straight on.
void PathAt(const Guide &g, float s, Vector2 &point, Vector2 &dir) {
  const std::vector<Vector2> &line = g.line;
  int last = static_cast<int>(line.size()) - 1;
  int k = std::clamp(static_cast<int>(std::floor(s)), 0, last - 1);
  Vector2 a = line[k], b = line[k + 1];
  dir = {b.x - a.x, b.y - a.y}; // points are a cell apart
  float t = s - k;
  point = {a.x + dir.x * t, a.y + dir.y * t};
}

// Where something at `pos` moving at `vel` can be met by a spell leaving
// `from` at `speed`: the earliest time both reach the same point. Where it
// is now when it can't be caught.
Vector2 Intercept(Vector2 from, float speed, Vector2 pos, Vector2 vel) {
  Vector2 d{pos.x - from.x, pos.y - from.y};
  float a = vel.x * vel.x + vel.y * vel.y - speed * speed;
  float b = 2.0f * (d.x * vel.x + d.y * vel.y);
  float c = d.x * d.x + d.y * d.y;
  float t = -1.0f;
  if (std::abs(a) < 1e-4f) {
    if (b < 0.0f)
      t = -c / b;
  } else {
    float disc = b * b - 4.0f * a * c;
    if (disc >= 0.0f) {
      float root = std::sqrt(disc);
      float t1 = (-b - root) / (2.0f * a), t2 = (-b + root) / (2.0f * a);
      if (t1 > t2)
        std::swap(t1, t2);
      t = t1 > 0.0f ? t1 : t2;
    }
  }
  if (t <= 0.0f)
    return pos;
  return {pos.x + vel.x * t, pos.y + vel.y * t};
}

bool Flying(const Particle &p) {
  return p.active && p.isProjectile && p.remainingDistance > 0.0f;
}

// The nearest cell of an element within `radius` of `from`, ring by ring
bool NearestCell(const Grid &grid, Vector2 from, Element element, int radius,
                 Vector2 &out) {
  int cx = static_cast<int>(std::floor(from.x));
  int cy = static_cast<int>(std::floor(from.y));
  for (int r = 0; r <= radius; ++r) {
    for (int dy = -r; dy <= r; ++dy) {
      int step = (dy == -r || dy == r) ? 1 : 2 * r; // ring edges only
      for (int dx = -r; dx <= r; dx += std::max(1, step)) {
        int x = cx + dx, y = cy + dy;
        if (grid.InBounds(x, y) && grid.Get(x, y).element == element) {
          out = {x + 0.5f, y + 0.5f};
          return true;
        }
      }
    }
  }
  return false;
}

} // namespace

int ParticleSystem::CreateGuide(const SpellStats &stats, Vector2 origin,
                                Vector2 dir, int owner) {
  Guide g;
  g.id = m_nextGuideId++;
  g.owner = owner;
  g.speed = stats.speed;
  g.steerTime = stats.steerTime;
  g.steerRate = stats.steerRate;
  g.homeTarget = static_cast<uint8_t>(stats.homeTarget);
  g.homeElement = stats.homeElement;
  g.homeTurnRate = stats.homeTurnRate;
  g.homeRadius = stats.homeRadius;
  g.heading = std::atan2(dir.y, dir.x);
  // Straight ahead to start with; Steer lays down the rest as it's reached
  g.line = {origin, {origin.x + dir.x, origin.y + dir.y}};
  m_guides.push_back(std::move(g));
  return m_guides.back().id;
}

Guide *ParticleSystem::FindGuide(int id) {
  for (Guide &g : m_guides)
    if (g.id == id)
      return &g;
  return nullptr;
}

void ParticleSystem::Follow(Particle &p, int guideId) {
  const Guide *g = FindGuide(guideId);
  if (!g)
    return;
  // Where it stands relative to the start of the path: along and across
  Vector2 origin = g->line[0];
  Vector2 dir{g->line[1].x - origin.x, g->line[1].y - origin.y};
  Vector2 rel{p.pos.x - origin.x, p.pos.y - origin.y};
  p.guideId = guideId;
  p.pathS = rel.x * dir.x + rel.y * dir.y;
  p.pathL = rel.x * -dir.y + rel.y * dir.x;
}

void ParticleSystem::Steer(const Grid &grid, float dt) {
  if (m_guides.empty())
    return;

  // How far along the leading particle of each guide is; a guide nothing
  // follows any more (landed, out of range) is dropped
  std::vector<float> lead(m_guides.size(), -1e9f);
  std::vector<bool> used(m_guides.size(), false);
  for (const Particle &p : m_particles) {
    if (!Flying(p) || p.guideId < 0)
      continue;
    for (size_t i = 0; i < m_guides.size(); ++i)
      if (m_guides[i].id == p.guideId) {
        used[i] = true;
        lead[i] = std::max(lead[i], p.pathS);
      }
  }

  for (size_t i = 0; i < m_guides.size(); ++i) {
    Guide &g = m_guides[i];
    if (!used[i])
      continue;
    Vector2 tip = g.line.back();

    // What the tip turns toward: the cursor while the sights set lasts,
    // then guidance's target, else straight on
    bool found = false;
    Vector2 target{0.0f, 0.0f};
    float rate = 0.0f;
    if (g.steerTime > 0.0f) {
      g.steerTime = std::max(0.0f, g.steerTime - dt);
      for (const Cursor &c : m_cursors)
        if (c.owner == g.owner) {
          target = c.pos;
          found = true;
        }
      rate = g.steerRate;
    } else if (g.homeTarget == static_cast<uint8_t>(HomeTarget::Human)) {
      float best = g.homeRadius * g.homeRadius;
      for (const Hurtbox &box : m_hurtboxes) {
        if (box.id == g.owner)
          continue;
        Vector2 c{box.bounds.x + box.bounds.width * 0.5f,
                  box.bounds.y + box.bounds.height * 0.5f};
        float d = (c.x - tip.x) * (c.x - tip.x) + (c.y - tip.y) * (c.y - tip.y);
        if (d <= best) {
          best = d;
          target = c;
          found = true;
        }
      }
      rate = g.homeTurnRate;
    } else if (g.homeTarget == static_cast<uint8_t>(HomeTarget::Element)) {
      // An enemy's spell of that element in flight comes first, aimed at
      // where the two can meet; else the nearest cells
      float best = g.homeRadius * g.homeRadius;
      for (const Particle &q : m_particles) {
        if (!Flying(q) || q.element != g.homeElement || q.owner == g.owner)
          continue;
        float dx = q.pos.x - tip.x, dy = q.pos.y - tip.y;
        float d = dx * dx + dy * dy;
        if (d > best)
          continue;
        best = d;
        target = Intercept(tip, g.speed, q.pos, q.vel);
        found = true;
      }
      if (!found)
        found = NearestCell(grid, tip, g.homeElement,
                            static_cast<int>(g.homeRadius), target);
      rate = g.homeTurnRate;
    }
    if (found) {
      float want = std::atan2(target.y - tip.y, target.x - tip.x);
      g.heading +=
          std::clamp(WrapAngle(want - g.heading), -rate * dt, rate * dt);
    }

    // Lay the path down ahead of the leader, far enough for this tick
    float needed = lead[i] + g.speed * dt + kLookAhead + 2.0f;
    Vector2 step{std::cos(g.heading), std::sin(g.heading)};
    while (static_cast<float>(g.line.size()) < needed) {
      Vector2 end = g.line.back();
      g.line.push_back({end.x + step.x, end.y + step.y});
    }
  }
  std::vector<Guide> kept;
  for (size_t i = 0; i < m_guides.size(); ++i)
    if (used[i])
      kept.push_back(std::move(m_guides[i]));
  m_guides = std::move(kept);

  // Each particle heads for its own spot a little further along the path,
  // as far across it as where it started: the figure keeps its shape
  for (Particle &p : m_particles) {
    if (!Flying(p) || p.guideId < 0)
      continue;
    const Guide *g = FindGuide(p.guideId);
    if (!g)
      continue;
    Vector2 point, dir;
    PathAt(*g, p.pathS + kLookAhead, point, dir);
    Vector2 aim{point.x - dir.y * p.pathL - p.pos.x,
                point.y + dir.x * p.pathL - p.pos.y};
    float len = std::hypot(aim.x, aim.y);
    float speed = std::hypot(p.vel.x, p.vel.y);
    if (len > 1e-4f)
      p.vel = {aim.x / len * speed, aim.y / len * speed};
  }
}

void ParticleSystem::Collide(ElementContext &ctx, float dt) {
  // Flying spell particles with a body, in pool order (every client pairs
  // them the same way)
  std::vector<Particle *> flying;
  for (Particle &p : m_particles)
    if (Flying(p) && p.castId >= 0 && BodyOf(p.element) != Body::None)
      flying.push_back(&p);
  if (flying.size() < 2)
    return;

  // What each side weighs is its whole spell, not the one drop: a ball
  // isn't broken up by a stray droplet, a boulder by a pebble
  std::map<int, int> castSize;
  for (const Particle *p : flying)
    castSize[p->castId]++;
  auto castMass = [&](const Particle &p) {
    return ElementMass(ctx.config, p.element) * castSize[p.castId];
  };

  std::vector<bool> met(flying.size(), false);
  for (size_t i = 0; i < flying.size(); ++i) {
    for (size_t j = i + 1; j < flying.size() && !met[i]; ++j) {
      Particle &a = *flying[i];
      Particle &b = *flying[j];
      if (met[j] || a.castId == b.castId)
        continue;
      // Closest they come this tick, moving as they are
      Vector2 r{a.pos.x - b.pos.x, a.pos.y - b.pos.y};
      Vector2 w{a.vel.x - b.vel.x, a.vel.y - b.vel.y};
      float ww = w.x * w.x + w.y * w.y;
      float t = ww > 0.0f ? std::clamp(-(r.x * w.x + r.y * w.y) / ww, 0.0f, dt)
                          : 0.0f;
      Vector2 c{r.x + w.x * t, r.y + w.y * t};
      if (c.x * c.x + c.y * c.y > 1.0f)
        continue;

      Body ba = BodyOf(a.element), bb = BodyOf(b.element);
      if (ba == Body::Flame && bb == Body::Flame)
        continue; // fire merges with fire
      met[i] = met[j] = true;
      // Sort the pair so each rule below is written once
      Particle *x = &a, *y = &b;
      if (ba > bb) {
        std::swap(x, y);
        std::swap(ba, bb);
      }
      if (ba == Body::Flame && bb == Body::Liquid) {
        // Water puts the fire out and carries on, a little spent
        Quench(*x);
        if (RandomUnit(ctx) < kClash.quenchChance)
          Quench(*y);
        else
          Slow(*y, kClash.quenchedSpeed);
      } else if (bb == Body::Solid && ba != Body::Solid) {
        // Water or fire splashes off a solid, which barely slows down
        Vector2 push = y->vel;
        Splash(*x, push, ctx);
        Slow(*y, ba == Body::Liquid ? kClash.throughLiquid
                                    : kClash.throughFlame);
      } else if (ba == Body::Liquid && bb == Body::Liquid) {
        // Much more water takes the smaller spell's drop in and carries on;
        // two spells of a size splash each other apart
        float mx = castMass(*x), my = castMass(*y);
        if (mx > my * kClash.absorbRatio || my > mx * kClash.absorbRatio) {
          if (mx > my)
            Absorb(*y, *x);
          else
            Absorb(*x, *y);
        } else {
          Vector2 va = x->vel, vb = y->vel;
          Splash(*x, vb, ctx);
          Splash(*y, va, ctx);
        }
      } else {
        // Solid on solid: they trade momentum by the mass of their spells,
        // and what's left too slow to fly falls
        float ma = castMass(*x);
        float mb = castMass(*y);
        Vector2 n{x->pos.x - y->pos.x, x->pos.y - y->pos.y};
        float len = std::hypot(n.x, n.y);
        if (len < 1e-4f) {
          n = {-w.x, -w.y};
          len = std::hypot(n.x, n.y);
        }
        if (len < 1e-4f)
          continue;
        n = {n.x / len, n.y / len};
        float closing = (x->vel.x - y->vel.x) * n.x + (x->vel.y - y->vel.y) * n.y;
        if (closing >= 0.0f)
          continue; // already parting
        float impulse = -(1.0f + kClash.restitution) * closing /
                        (1.0f / ma + 1.0f / mb);
        for (auto [p, m, sign] : {std::tuple{x, ma, 1.0f},
                                  std::tuple{y, mb, -1.0f}}) {
          float before = std::hypot(p->vel.x, p->vel.y);
          p->vel.x += sign * impulse / m * n.x;
          p->vel.y += sign * impulse / m * n.y;
          float after = std::hypot(p->vel.x, p->vel.y);
          if (before > 0.0f)
            p->power *= (after / before) * (after / before);
          if (after < kClash.dropSpeed)
            LetGo(*p);
        }
      }
    }
  }
}

void ParticleSystem::Absorb(Particle &drop, const Particle &into) {
  // It becomes part of the bigger spell: its caster, speed and path
  Vector2 pos = drop.pos;
  drop = into;
  drop.pos = pos;
  if (into.guideId >= 0)
    Follow(drop, into.guideId);
}

void ParticleSystem::Burst(Particle &p) {
  p.active = false;
  if (p.flashRadius <= 0.0f)
    return;
  m_flashes.push_back({p.pos, p.flashRadius, p.flashTime, p.owner});
  m_visualFlashes.push_back({p.pos, p.flashRadius, 0.0f});
  ++m_burstCount;
  m_lastBurstPos = p.pos;
}

bool ParticleSystem::HitHurtbox(Particle &p) {
  for (const Hurtbox &box : m_hurtboxes) {
    if (box.id == p.owner || box.id == p.lastHit ||
        !CheckCollisionPointRec(p.pos, box.bounds))
      continue;
    m_hits.push_back({box.id, p.owner, p.power, p.element});
    Note({ParticleNoise::Impact, p.element, p.pos});
    p.lastHit = box.id;
    // Flame has no body to stop it: it sets the target alight on its way
    return p.element != Element::FIRE;
  }
  return false;
}

void ParticleSystem::Update(Grid &grid, ElementContext &ctx, float dt) {
  m_flashes.clear(); // only this tick's, whether or not anyone took them
  for (VisualFlash &f : m_visualFlashes)
    f.age += dt;
  std::erase_if(m_visualFlashes,
                [](const VisualFlash &f) { return f.age > 0.5f; });
  Steer(grid, dt);
  Collide(ctx, dt);

  for (auto &p : m_particles) {
    if (!p.active)
      continue;

    bool flying = p.isProjectile && p.remainingDistance > 0.0f;
    if (!flying)
      p.vel.y += ctx.config.world.gravity * 20.0f * dt;

    Vector2 movement = {p.vel.x * dt, p.vel.y * dt};
    float travelDistance =
        std::sqrt(movement.x * movement.x + movement.y * movement.y);
    int steps = std::max(1, (int)std::ceil(travelDistance));
    Vector2 stepDelta = {movement.x / steps, movement.y / steps};
    float stepLength = travelDistance / steps;

    for (int step = 0; step < steps; ++step) {
      Vector2 nextPos = {p.pos.x + stepDelta.x, p.pos.y + stepDelta.y};
      int tx = (int)std::floor(nextPos.x);
      int ty = (int)std::floor(nextPos.y);

      bool light = p.element == Element::LIGHT;
      if (!grid.InBounds(tx, ty)) {
        // The world edge acts as a wall
        if (light)
          Burst(p);
        else
          Deposit(p, grid, ctx);
        break;
      }

      bool flyingNow = p.isProjectile && p.remainingDistance > 0.0f;
      if (flyingNow && grid.Get(tx, ty).element != Element::AIR) {
        ApplyHitModifiers(p, grid, ctx, tx, ty); // may turn rock into sand
        if (!MeetCell(p, grid, ctx, tx, ty))
          break; // put out, or gone up in steam, where it met the cell
      }
      const Cell &target = grid.Get(tx, ty);
      const auto &targetProps =
          ctx.config.elements[static_cast<size_t>(target.element)];
      if (target.element != Element::AIR && !targetProps.passable) {
        // Projectiles slip through liquids rather than erasing them
        bool liquid = targetProps.mobile && !targetProps.solid;
        bool penetrated =
            flyingNow && (liquid || TryImpact(p, grid, ctx, tx, ty));
        // Liquid projectiles splash apart on impact: they can knock out one
        // cell but don't keep boring through like solid ones
        const auto &selfProps =
            ctx.config.elements[static_cast<size_t>(p.element)];
        if (penetrated && !liquid && selfProps.mobile && !selfProps.solid)
          p.power = 0.0f;
        if (!penetrated) {
          // Water that lands on something burning puts it out around it
          if (BodyOf(p.element) == Body::Liquid)
            Douse(grid, ctx, tx, ty);
          if (light)
            Burst(p);
          else
            Deposit(p, grid, ctx);
          break;
        }
      }

      p.pos = nextPos;

      if (p.isProjectile && p.remainingDistance > 0.0f && HitHurtbox(p)) {
        if (light)
          Burst(p);
        p.active = false;
        break;
      }

      if (p.isProjectile) {
        p.pathS += stepLength;
        p.remainingDistance -= stepLength;
        if (p.remainingDistance <= 0.0f) {
          if (light) {
            Burst(p); // light doesn't fall: it goes off where it stops
            break;
          }
          // Out of range: the spell lets go and the element falls naturally
          p.isProjectile = false;
          p.power = 0.0f;
        }
      }
    }
  }
}

void ParticleSystem::Clear() {
  for (auto &p : m_particles)
    p.active = false;
  m_hits.clear();
  m_flashes.clear();
  m_noises.clear();
  m_visualFlashes.clear();
  m_cursors.clear();
  m_guides.clear();
  m_nextGuideId = 0;
  m_nextCastId = 0;
}

void ParticleSystem::Draw() {
  for (const auto &p : m_particles) {
    if (p.active) {
      // Simplified: Draw a rectangle with approximate element color
      Color color;
      switch (p.element) {
      case Element::WATER:
        color = {40, 140, 220, 255};
        break;
      case Element::SAND:
        color = {220, 180, 100, 255};
        break;
      case Element::EARTH:
        color = {90, 55, 30, 255};
        break;
      case Element::ROCK:
        color = {100, 100, 100, 255};
        break;
      case Element::FIRE:
        color = {240, 110, 20, 255};
        break;
      case Element::ICE:
        color = {160, 230, 255, 255};
        break;
      case Element::STEAM:
        color = {200, 200, 215, 255};
        break;
      case Element::SMOKE:
        color = {110, 105, 105, 255};
        break;
      case Element::LIGHT:
        color = {255, 250, 215, 255};
        // A soft glow around each mote
        DrawCircleV({(p.pos.x + 0.5f) * CELL_SIZE, (p.pos.y + 0.5f) * CELL_SIZE},
                    CELL_SIZE * 2.0f, Color{255, 245, 190, 70});
        break;
      default:
        color = WHITE;
        break;
      }
      DrawRectangle((int)(p.pos.x * CELL_SIZE), (int)(p.pos.y * CELL_SIZE),
                    CELL_SIZE, CELL_SIZE, color);
    }
  }
  // Bursts of light: a bright disc that swells and fades
  for (const VisualFlash &f : m_visualFlashes) {
    float t = f.age / 0.5f;
    float r = f.radius * (0.4f + 0.6f * t) * CELL_SIZE;
    unsigned char alpha = static_cast<unsigned char>(200 * (1.0f - t));
    DrawCircleGradient({f.pos.x * CELL_SIZE, f.pos.y * CELL_SIZE}, r,
                       Color{255, 255, 240, alpha}, Color{255, 240, 180, 0});
  }
}
