#include "whas/physics/particle_system.h"
#include "whas/constants.h"
#include "whas/core/config.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/world/grid.h"
#include <algorithm>
#include <cmath>

ParticleSystem::ParticleSystem(int maxParticles)
    : m_maxParticles(maxParticles) {
  m_particles.resize(maxParticles);
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
      p.temperature = 0.0f;
      return &p;
    }
  }
  return nullptr;
}

namespace {

// Loose rock would immediately be re-extracted and crumbled by the rigid body
// system, so it settles as rubble instead.
Element DepositedElement(Element element) {
  return element == Element::ROCK ? Element::SAND : element;
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

  Element debris = granular ? target.element : RubbleOf(target.element);
  float temperature = target.temperature;
  target = ElementFactory::Create(Element::AIR, ctx.config);
  ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex, props.staticTerrain);
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

        Element element = DepositedElement(p.element);
        Cell cell = ElementFactory::Create(element, ctx.config);
        if (p.temperature > 0.0f)
          cell.temperature = p.temperature;
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

} // namespace

bool ParticleSystem::HitHurtbox(const Particle &p) {
  for (const Hurtbox &box : m_hurtboxes) {
    if (box.id == p.owner || !CheckCollisionPointRec(p.pos, box.bounds))
      continue;
    m_hits.push_back({box.id, p.owner, p.power});
    return true;
  }
  return false;
}

void ParticleSystem::Update(Grid &grid, ElementContext &ctx, float dt) {
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

      if (!grid.InBounds(tx, ty)) {
        // The world edge acts as a wall
        Deposit(p, grid, ctx);
        break;
      }

      const Cell &target = grid.Get(tx, ty);
      const auto &targetProps =
          ctx.config.elements[static_cast<size_t>(target.element)];
      if (target.element != Element::AIR && !targetProps.passable) {
        bool flyingNow = p.isProjectile && p.remainingDistance > 0.0f;
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
          Deposit(p, grid, ctx);
          break;
        }
      }

      p.pos = nextPos;

      if (p.isProjectile && p.remainingDistance > 0.0f && HitHurtbox(p)) {
        p.active = false;
        break;
      }

      if (p.isProjectile) {
        p.remainingDistance -= stepLength;
        if (p.remainingDistance <= 0.0f) {
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
      default:
        color = WHITE;
        break;
      }
      DrawRectangle((int)(p.pos.x * CELL_SIZE), (int)(p.pos.y * CELL_SIZE),
                    CELL_SIZE, CELL_SIZE, color);
    }
  }
}
