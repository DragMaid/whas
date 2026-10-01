#include "whas/engine/simulation.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/registry.h"
#include "whas/physics/heat_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <utility>

Simulation::Simulation(int workerThreads)
    : m_config(), m_frameConfig(), m_rng(m_seed),
      m_chunkSpawns(CHUNK_COLS * CHUNK_ROWS), m_grid(m_config), m_particles(),
      m_numThreads(std::max(1, workerThreads)),
      m_syncBarrier(m_numThreads + 1, [this]() { m_currentPass++; }) {

  for (int i = 0; i < m_numThreads; ++i) {
    m_workers.emplace_back(
        [this, i](std::stop_token st) { WorkerLoop(i, st); });
  }
}

void Simulation::SetSeed(uint64_t seed) {
  m_seed = seed;
  m_rng = DetRng(seed);
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
                     m_particles};

  PressureSystem::Update(ctx);

  // 2. Run falling-sand simulation on worker threads
  {
    std::lock_guard lock(m_wakeMutex);
    m_workerFrame = m_frameCounter;
    ++m_workGeneration;
  }
  m_wakeCv.notify_all();

  for (int p = 0; p < 5; ++p) {
    m_syncBarrier.arrive_and_wait();
  }
  FlushWorkerSpawns();

  // Damage check: release bodies whose pixels were erased, painted over,
  // broken by last frame's projectiles or changed by this frame's elements
  // (ice melting). It must run before the bodies move, or a changed pixel
  // is no longer where the body looks for it.
  m_rigidBodies.PreUpdate(m_grid, ctx);

  // 3. Extract new rigid bodies from freshly painted pixels (skip while
  // painting to avoid extracting a body from an incomplete stroke).
  if (!isPainting) {
    m_rigidBodies.ExtractDynamicBodies(m_grid, ctx, m_particles);
  }

  // 4. Clear, mesh, step, displace, sync
  m_rigidBodies.PostUpdate(m_grid, ctx, m_particles, m_frameConfig, dt);

  // 5. Spell emission, then particle update
  SpellSystem::TickEffects(m_activeSpellEffects, ctx, m_rigidBodies, dt);
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
  uint64_t lastGeneration = 0;
  int numThreads = m_numThreads;

  while (!stopToken.stop_requested() && m_running) {
    {
      std::unique_lock<std::mutex> lock(m_wakeMutex);
      m_wakeCv.wait(lock,
                    [&] { return m_workGeneration > lastGeneration || !m_running; });
    }
    if (!m_running)
      break;

    uint32_t currentFrame = m_workerFrame;
    lastGeneration = m_workGeneration;
    // Placeholder stream; UpdateChunk gives each chunk its own
    DetRng unused;
    ElementContext ctx{m_grid,
                       m_chunks,
                       unused,
                       m_frameConfig,
                       currentFrame,
                       m_particles};

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
  }
}

void Simulation::UpdateChunk(int chunkIdx, const ElementContext &base) {
  // The stream depends on the chunk and frame, not the thread running it
  DetRng rng(m_seed, (static_cast<uint64_t>(base.frameIndex) << 20) |
                         static_cast<uint64_t>(chunkIdx));
  ElementContext ctx{base.grid,   base.chunks,     rng,
                     base.config, base.frameIndex, base.particles,
                     &m_chunkSpawns[chunkIdx]};

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

void Simulation::FlushWorkerSpawns() {
  for (auto &spawns : m_chunkSpawns) {
    for (const PendingSpawn &s : spawns)
      m_particles.Spawn(s.pos, s.vel, s.element);
    spawns.clear();
  }
}

namespace {
struct Fnv {
  uint64_t h = 0xcbf29ce484222325ull;
  template <typename T> void Add(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &v, sizeof(T));
    for (unsigned char b : bytes) {
      h ^= b;
      h *= 0x100000001b3ull;
    }
  }
  void AddVec(Vector2 v) { Add(v.x); Add(v.y); }
};
} // namespace

uint64_t Simulation::StateHash() const {
  // Field by field, so struct padding never leaks into the hash
  Fnv f;
  f.Add(m_frameCounter);
  for (const Cell &c : m_grid.GetBuffer()) {
    f.Add(static_cast<uint8_t>(c.element));
    if (c.element == Element::AIR)
      continue;
    f.Add(c.temperature);
    f.Add(c.pressure);
    f.Add(c.mass);
    f.Add(c.vx);
    f.Add(c.vy);
    f.Add(c.bodyID);
    f.Add(c.lifetime);
    f.Add(c.hardness);
    f.Add(c.moisture);
    f.Add(c.flags);
  }
  const_cast<ParticleSystem &>(m_particles).ForEachActive([&](Particle &p) {
    f.AddVec(p.pos);
    f.AddVec(p.vel);
    f.Add(static_cast<uint8_t>(p.element));
    f.Add(p.remainingDistance);
    f.Add(p.power);
    f.Add(p.owner);
  });
  for (const Guide &g : m_particles.Guides()) {
    f.Add(g.id);
    f.Add(g.heading);
    f.Add(g.steerTime);
    f.AddVec(g.line.back());
  }
  for (const SpellEffect &e : m_activeSpellEffects) {
    f.Add(static_cast<uint8_t>(e.stats.kind));
    f.AddVec(e.origin);
    f.AddVec(e.direction);
    f.Add(e.emitted);
    f.Add(e.timeRemaining);
  }
  f.Add(m_rigidBodies.StateHash());
  return f.h;
}

void Simulation::CastSpell(const Spell &spell, Vector2 origin,
                           Vector2 aimDirection, int owner) {
  CastSpell(SpellSystem::Evaluate(spell), origin, aimDirection, owner);
}

void Simulation::CastSpell(const SpellStats &stats, Vector2 origin,
                           Vector2 aimDirection, int owner) {
  // A layered spell fires every part at once, each as its own effect
  if (stats.kind == SpellKind::Compound) {
    if (stats.valid)
      for (const SpellStats &part : stats.parts)
        CastSpell(part, origin, aimDirection, owner);
    return;
  }

  SpellEffect effect;
  effect.stats = stats;
  // Flight moves the caster, which the game layer handles
  if (!effect.stats.valid || effect.stats.kind == SpellKind::Flight)
    return;
  effect.timeRemaining = effect.stats.duration;
  effect.origin = origin;
  effect.direction = SpellSystem::ResolveDirection(effect.stats, aimDirection);
  effect.owner = owner;
  effect.castId = m_particles.NewCastId();
  // Sights set and guidance steer the whole figure along one path
  if (effect.stats.kind == SpellKind::Element &&
      (stats.steerTime > 0.0f || stats.homeTarget != HomeTarget::None))
    effect.guideId =
        m_particles.CreateGuide(stats, origin, effect.direction, owner);
  if (stats.collectMax > 0)
    effect.bonusParticles = Collect(stats, origin);
  m_activeSpellEffects.push_back(effect);
  if (effect.stats.kind == SpellKind::Element)
    m_particles.Note({ParticleNoise::Cast, effect.stats.element, origin});
}

namespace {

// What collection can draw in for a spell of this element
bool Collectable(Element spell, const Cell &cell) {
  if (cell.bodyID >= 0)
    return false; // rigid bodies stay whole
  switch (spell) {
  case Element::WATER:
  case Element::ICE:
    return cell.element == Element::WATER || cell.element == Element::ICE;
  case Element::EARTH:
  case Element::SAND:
  case Element::ROCK:
    return cell.element == Element::EARTH || cell.element == Element::SAND;
  case Element::FIRE:
    return cell.element == Element::FIRE;
  default:
    return false;
  }
}

} // namespace

int Simulation::Collect(const SpellStats &stats, Vector2 origin) {
  int r = static_cast<int>(std::ceil(stats.collectRadius));
  int cx = static_cast<int>(std::floor(origin.x));
  int cy = static_cast<int>(std::floor(origin.y));
  float r2 = stats.collectRadius * stats.collectRadius;
  int taken = 0;
  // Row by row from the top: the same cells go on every client
  for (int y = cy - r; y <= cy + r && taken < stats.collectMax; ++y) {
    for (int x = cx - r; x <= cx + r && taken < stats.collectMax; ++x) {
      float dx = x + 0.5f - origin.x, dy = y + 0.5f - origin.y;
      if (dx * dx + dy * dy > r2 || !m_grid.InBounds(x, y))
        continue;
      Cell &c = m_grid.Get(x, y);
      if (!Collectable(stats.element, c))
        continue;
      bool wasStatic =
          m_config.elements[static_cast<size_t>(c.element)].staticTerrain;
      c = ElementFactory::Create(Element::AIR, m_config);
      m_chunks.WakeChunkAt(x, y, m_frameCounter, wasStatic);
      taken++;
    }
  }
  return taken;
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

void Simulation::Reset() {
  // Erasing body cells makes the rigid body system drop those bodies on the
  // next update, the same as when the player erases them by hand
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (m_grid.Get(x, y).element != Element::AIR)
        Erase(x, y, 0);
  m_particles.Clear();
  m_activeSpellEffects.clear();
}

void Simulation::Restart(uint64_t seed) {
  m_grid = Grid(m_config);
  m_chunks = ChunkManager();
  m_rigidBodies.Reset();
  m_particles.Clear();
  m_particles.SetHurtboxes({});
  m_activeSpellEffects.clear();
  for (auto &spawns : m_chunkSpawns)
    spawns.clear();
  m_frameCounter = 0;
  SetSeed(seed);
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
