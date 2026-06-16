#include "whas/physics/particle_system.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/world/grid.h"

ParticleSystem::ParticleSystem(int maxParticles)
    : m_maxParticles(maxParticles) {
  m_particles.resize(maxParticles);
}

void ParticleSystem::Spawn(Vector2 pos, Vector2 vel, Element element) {
  for (auto &p : m_particles) {
    if (!p.active) {
      p.pos = pos;
      p.vel = vel;
      p.element = element;
      p.active = true;
      return;
    }
  }
}

void ParticleSystem::Update(Grid &grid, ElementContext &ctx, float dt) {
  for (auto &p : m_particles) {
    if (!p.active)
      continue;

    // Apply gravity
    p.vel.y +=
        ctx.config.world.gravity * 20.0f * dt; // Scale gravity for particles

    Vector2 nextPos = {p.pos.x + p.vel.x * dt, p.pos.y + p.vel.y * dt};

    int tx = (int)std::floor(nextPos.x);
    int ty = (int)std::floor(nextPos.y);

    if (!grid.InBounds(tx, ty)) {
      p.active = false;
      continue;
    }

    const Cell &target = grid.Get(tx, ty);
    if (target.element != Element::AIR) {
      // Hit something! Turn back into element if possible.
      // Search for nearby AIR spot to place the element
      bool placed = false;
      for (int dy = -1; dy <= 1 && !placed; ++dy) {
        for (int dx = -1; dx <= 1 && !placed; ++dx) {
          if (grid.InBounds(tx + dx, ty + dy)) {
            Cell &dest = grid.Get(tx + dx, ty + dy);
            if (dest.element == Element::AIR) {
              dest = ElementFactory::Create(p.element, ctx.config);
              dest.vx = p.vel.x * 0.1f;
              dest.vy = p.vel.y * 0.1f;
              ctx.chunks.WakeChunkAt(tx + dx, ty + dy, ctx.frameIndex);
              placed = true;
            }
          }
        }
      }
      p.active = false;
    } else {
      p.pos = nextPos;
    }
  }
}

void ParticleSystem::Draw() {
  for (const auto &p : m_particles) {
    if (p.active) {
      // Simplified: Draw a pixel with approximate element color
      Color color;
      switch (p.element) {
      case Element::WATER:
        color = BLUE;
        break;
      case Element::SAND:
        color = GOLD;
        break;
      default:
        color = WHITE;
        break;
      }
      DrawPixel((int)(p.pos.x * CELL_SIZE), (int)(p.pos.y * CELL_SIZE), color);
    }
  }
}
