#include "whas/physics/particle_system.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/spell/spell_system.h"
#include "whas/world/grid.h"
#include <algorithm>
#include <cmath>

ParticleSystem::ParticleSystem(int maxParticles)
    : m_maxParticles(maxParticles) {
  m_particles.resize(maxParticles);
}

void ParticleSystem::Spawn(Vector2 pos, Vector2 vel, Element element,
                           float remainingDistance, bool spellActive,
                           bool isProjectile) {
  for (auto &p : m_particles) {
    if (!p.active) {
      p.pos = pos;
      p.vel = vel;
      p.element = element;
      p.active = true;
      p.spellActive = spellActive;
      p.isProjectile = isProjectile;
      p.remainingDistance = remainingDistance;
      return;
    }
  }
}

namespace {

void ResolveParticleCollision(Particle &p, Grid &grid, ElementContext &ctx,
                              int tx, int ty) {
  const Cell &target = grid.Get(tx, ty);
  const auto &targetProps =
      ctx.config.elements[static_cast<size_t>(target.element)];

  if (targetProps.passable)
    return;

  const auto &sourceProps = ctx.config.elements[static_cast<size_t>(p.element)];
  float mass = sourceProps.defaultMass > 0.0f ? sourceProps.defaultMass : 1.0f;
  float speedSq = p.vel.x * p.vel.x + p.vel.y * p.vel.y;
  float kineticEnergy = 0.5f * mass * speedSq;

  if (kineticEnergy >= target.hardness) {
    Cell air = ElementFactory::Create(Element::AIR, ctx.config);
    grid.Get(tx, ty) = air;
    ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex, true);

    float remainingEnergy = kineticEnergy - target.hardness;
    float newSpeed = (remainingEnergy > 0.0f)
                         ? std::sqrt((2.0f * remainingEnergy) / mass)
                         : 0.0f;
    float speed = std::sqrt(speedSq);
    float scale = (speed > 0.001f) ? newSpeed / speed : 0.0f;
    p.vel.x *= scale;
    p.vel.y *= scale;
  } else {
    if (std::abs(p.vel.x) > std::abs(p.vel.y)) {
      p.vel.x = 0.0f;
    } else {
      p.vel.y = 0.0f;
    }
    p.spellActive = false;
  }
}

bool ParticleInSpellEffect(const Particle &p, const SpellEffect &effect) {
  int tx = static_cast<int>(std::floor(p.pos.x));
  int ty = static_cast<int>(std::floor(p.pos.y));
  return std::any_of(effect.affectedCells.begin(), effect.affectedCells.end(),
                     [&](const Vector2 &cell) {
                       return static_cast<int>(cell.x) == tx &&
                              static_cast<int>(cell.y) == ty;
                     });
}

void ApplyActiveSpellEffects(Particle &p, ElementContext &ctx) {
  if (!ctx.activeSpellEffects)
    return;
  for (SpellEffect &effect : *ctx.activeSpellEffects) {
    if (!effect.active)
      continue;
    if (!ParticleInSpellEffect(p, effect))
      continue;
    if (std::find(effect.targetedElements.begin(),
                  effect.targetedElements.end(),
                  p.element) == effect.targetedElements.end()) {
      continue;
    }
    SpellSystem::ApplySpellEffectToParticle(p, effect, ctx);
  }
}

} // namespace

void ParticleSystem::Update(Grid &grid, ElementContext &ctx, float dt) {
  for (auto &p : m_particles) {
    if (!p.active)
      continue;

    Vector2 movement = {p.vel.x * dt, p.vel.y * dt};
    p.vel.y += ctx.config.world.gravity * 20.0f * dt;
    movement = {p.vel.x * dt, p.vel.y * dt};

    float travelDistance =
        std::sqrt(movement.x * movement.x + movement.y * movement.y);
    int steps = std::max(1, (int)std::ceil(travelDistance));
    Vector2 stepDelta = {movement.x / steps, movement.y / steps};

    bool alive = true;
    for (int step = 0; step < steps && alive; ++step) {
      Vector2 nextPos = {p.pos.x + stepDelta.x, p.pos.y + stepDelta.y};
      int tx = (int)std::floor(nextPos.x);
      int ty = (int)std::floor(nextPos.y);

      if (!grid.InBounds(tx, ty)) {
        p.active = false;
        alive = false;
        break;
      }

      // ApplyActiveSpellEffects(p, ctx);

      const Cell &target = grid.Get(tx, ty);
      if (target.element != Element::AIR) {
        ResolveParticleCollision(p, grid, ctx, tx, ty);
        if (p.spellActive) {
          p.pos = nextPos;
          continue;
        }

        alive = false;
        break;
      }

      p.pos = nextPos;
    }

    if (!alive)
      continue;
  }

  if (ctx.activeSpellEffects) {
    auto &effects = *ctx.activeSpellEffects;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
                       [&](SpellEffect &effect) {
                         if (!effect.active)
                           return true;
                         if (!effect.hadTargetInZone)
                           return false;
                         bool hasTargetInZone = false;
                         for (const auto &p : m_particles) {
                           if (!p.active)
                             continue;
                           if (std::find(effect.targetedElements.begin(),
                                         effect.targetedElements.end(),
                                         p.element) ==
                               effect.targetedElements.end())
                             continue;
                           if (ParticleInSpellEffect(p, effect)) {
                             hasTargetInZone = true;
                             break;
                           }
                         }
                         return !hasTargetInZone;
                       }),
        effects.end());
  }
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
      default:
        color = WHITE;
        break;
      }
      DrawRectangle((int)(p.pos.x * CELL_SIZE), (int)(p.pos.y * CELL_SIZE),
                    CELL_SIZE, CELL_SIZE, color);
    }
  }
}
