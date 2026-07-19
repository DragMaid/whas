#include "whas/engine/simulation.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/registry.h"
#include "whas/physics/heat_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>
#include <iostream>

Simulation::Simulation()
    : m_config(), m_frameConfig(), m_rng(42), m_grid(m_config), m_particles(),
      m_syncBarrier(std::thread::hardware_concurrency() + 1,
                    [this]() { m_currentPass++; }) {

  int numThreads = std::thread::hardware_concurrency();
  // int numThreads = 1;
  for (int i = 0; i < numThreads; ++i) {
    m_workers.emplace_back(
        [this, i](std::stop_token st) { WorkerLoop(i, st); });
  }
}

Simulation::~Simulation() {
  m_running = false;
  m_wakeCv.notify_all();
}

void Simulation::Update(float dt, bool isPainting) {
  m_lastDt = dt;
  m_frameCounter++;

  // BeginFrame marks chunk dirty flags — must happen before PreUpdate so that
  // UpdateWorldMeshes (called from PostUpdate) sees accurate
  // lastStaticChangeFrame values and can skip unchanged chunks correctly (FIX
  // 2).
  m_chunks.BeginFrame();
  m_frameConfig = m_config;

  ElementContext ctx{m_grid,
                     m_chunks,
                     m_rng,
                     m_frameConfig,
                     m_frameCounter,
                     m_particles,
                     &m_activeSpellEffects};

  // 0. Spell particles are updated as part of the normal particle system.

  // 1. Damage check — detect erased/painted-over body pixels and release them
  m_rigidBodies.PreUpdate(m_grid, ctx);

  PressureSystem::Update(ctx);

  // 2. Run falling-sand simulation on worker threads
  m_workerFrame = m_frameCounter;
  m_wakeCv.notify_all();

  for (int p = 0; p < 5; ++p) {
    m_syncBarrier.arrive_and_wait();
  }

  // 3. Extract new rigid bodies from freshly painted pixels (skip while
  // painting to avoid extracting a body from an incomplete stroke).
  if (!isPainting) {
    m_rigidBodies.ExtractDynamicBodies(m_grid, ctx, m_particles);
  }

  // 4. Clear, mesh, step, displace, sync
  m_rigidBodies.PostUpdate(m_grid, ctx, m_particles, m_frameConfig, dt);

  // 5. Particle update
  m_particles.Update(m_grid, ctx, dt);

  // 6. Heat & pressure propagation
  // TODO: this doesnt gain anything from the parallelism
  // TODO: re-consider this realistic heat transfer system later
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      HeatSystem::Propagate(x, y, ctx, dt);
      PressureSystem::Propagate(x, y, ctx);
    }
  }

  CollectStatistics();
}

void Simulation::WorkerLoop(int threadIdx, std::stop_token stopToken) {
  uint32_t lastFrame = 0;
  int numThreads = std::thread::hardware_concurrency();

  while (!stopToken.stop_requested() && m_running) {
    {
      std::unique_lock<std::mutex> lock(m_wakeMutex);
      m_wakeCv.wait(lock,
                    [&] { return m_workerFrame > lastFrame || !m_running; });
    }
    if (!m_running)
      break;

    uint32_t currentFrame = m_workerFrame;
    std::mt19937 threadRng(currentFrame + threadIdx);
    ElementContext ctx{m_grid,
                       m_chunks,
                       threadRng,
                       m_frameConfig,
                       currentFrame,
                       m_particles,
                       &m_activeSpellEffects};

    for (int pass = 0; pass < 4; ++pass) {
      int passX = pass % 2;
      int passY = pass / 2;

      for (int i = threadIdx; i < CHUNK_COLS * CHUNK_ROWS; i += numThreads) {
        int cx = i % CHUNK_COLS;
        int cy = i / CHUNK_COLS;
        if (cx % 2 == passX && cy % 2 == passY) {
          if (m_chunks.GetChunk(cx, cy).active) {
            UpdateChunk(i, ctx);
          }
        }
      }
      m_syncBarrier.arrive_and_wait();
    }

    m_syncBarrier.arrive_and_wait();
    lastFrame = currentFrame;
  }
}

void Simulation::UpdateChunk(int chunkIdx, ElementContext &ctx) {
  int chunkCol = chunkIdx % CHUNK_COLS;
  int chunkRow = chunkIdx / CHUNK_COLS;

  int x0 = chunkCol * CHUNK_SIZE;
  int y0 = chunkRow * CHUNK_SIZE;
  int x1 = std::min(x0 + CHUNK_SIZE, GRID_W);
  int y1 = std::min(y0 + CHUNK_SIZE, GRID_H);

  for (int y = y1 - 1; y >= y0; --y) {
    for (int x = x0; x < x1; ++x) {
      Cell &c = m_grid.Get(x, y);
      if (c.lastUpdateFrame == ctx.frameIndex)
        continue;
      if (c.element == Element::AIR)
        continue;
      ElementUpdateRegistry::Update(c.element, x, y, ctx);
    }
  }

  int count = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; ++x)
      if (m_grid.Get(x, y).element != Element::AIR)
        ++count;
  m_chunks.SetActiveCount(chunkCol, chunkRow, count);
}

void Simulation::UpdateElements() {}

void Simulation::CastSpell(const Spell &spell, Vector2 origin,
                           Vector2 mouseDirection) {
  Vector2 direction = SpellSystem::ComputeSpellDirection(spell, mouseDirection);
  float range = SpellSystem::ComputeSpellRange(spell);
  float speed = SpellSystem::ComputeSpellSpeed(spell);
  float diameter = SpellSystem::ComputeSpellDiameter(spell);
  int waveCount = SpellSystem::ComputeSpellWaveCount(spell);
  int particlesPerWave = SpellSystem::ComputeSpellParticleCount(spell);
  std::cout << "finished" << std::endl;

  SpellEffect effect;
  effect.spell = spell;
  effect.origin = origin;
  effect.direction = direction;
  effect.range = range;
  effect.speed = speed;
  effect.diameter = diameter;
  effect.waveSpacing = std::max(1.0f, range / std::max(1, waveCount));
  // TODO: this is computationally heavy, remove it
  effect.affectedCells = SpellSystem::ComputeSpellWavePositions(
      spell, origin, direction, range, diameter, waveCount, particlesPerWave);
  std::cout << "finished" << std::endl;
  effect.targetedElements = {SpellSystem::GetSpellElement(spell)};
  std::cout << "finished" << std::endl;
  effect.color = {200, 120, 80, 255};
  effect.active = true;
  effect.particleCount = particlesPerWave;
  m_activeSpellEffects.push_back(effect);
  std::cout << "finished" << std::endl;
}

void Simulation::Paint(int cx, int cy, Element element, int brushRadius) {
  for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
    for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
      if (dx * dx + dy * dy > brushRadius * brushRadius)
        continue;
      int x = cx + dx, y = cy + dy;
      if (!m_grid.InBounds(x, y))
        continue;
      Cell c = ElementFactory::Create(element, m_config);
      m_grid.Get(x, y) = c;
      const auto &props = m_config.elements[static_cast<size_t>(element)];
      m_chunks.WakeChunkAt(x, y, m_frameCounter, props.staticTerrain);
    }
  }
}

void Simulation::Erase(int cx, int cy, int brushRadius) {
  for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
    for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
      if (dx * dx + dy * dy > brushRadius * brushRadius)
        continue;
      int x = cx + dx, y = cy + dy;
      if (!m_grid.InBounds(x, y))
        continue;
      const Cell &c = m_grid.Get(x, y);
      const auto &props = m_config.elements[static_cast<size_t>(c.element)];
      bool wasStatic = props.staticTerrain;
      m_grid.Get(x, y) = ElementFactory::Create(Element::AIR, m_config);
      m_chunks.WakeChunkAt(x, y, m_frameCounter, wasStatic);
    }
  }
}

void Simulation::CollectStatistics() {
  m_particleCount = 0;
  double pSum = 0.0, tSum = 0.0;
  for (const auto &c : m_grid.GetBuffer()) {
    if (c.element != Element::AIR) {
      ++m_particleCount;
      pSum += c.pressure;
      tSum += c.temperature;
    }
  }
  if (m_particleCount > 0) {
    m_avgPressure = static_cast<float>(pSum / m_particleCount);
    m_avgTemp = static_cast<float>(tSum / m_particleCount);
  }
}
