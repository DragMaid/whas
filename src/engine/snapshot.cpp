#include "whas/core/bytes.h"
#include "whas/engine/simulation.h"
#include <cstring>

// Simulation snapshots for desync recovery. Fields are written one by one
// (no struct padding) and runs of identical cells are collapsed, so a
// typical arena is a few tens of KB.

namespace {

constexpr uint32_t MAGIC = 0x324E5357; // "WSN2"
constexpr uint16_t MAX_RUN = 0xFFFF;

// Cells are stored field by field ("columns"), each column run-length
// encoded: elements, masses and flags come in long runs even where
// temperatures or update frames vary cell to cell.
template <typename T, typename Get>
void PutColumn(ByteWriter &out, const std::vector<Cell> &cells, Get get) {
  for (size_t i = 0; i < cells.size();) {
    T v = get(cells[i]);
    uint16_t run = 1;
    while (i + run < cells.size() && run < MAX_RUN) {
      T next = get(cells[i + run]);
      if (std::memcmp(&next, &v, sizeof(T)) != 0)
        break;
      ++run;
    }
    out.Put(run);
    out.Put(v);
    i += run;
  }
}

template <typename T, typename Set>
void GetColumn(ByteReader &in, std::vector<Cell> &cells, Set set) {
  for (size_t i = 0; i < cells.size();) {
    uint16_t run = in.Get<uint16_t>();
    T v = in.Get<T>();
    if (run == 0 || i + run > cells.size())
      throw std::runtime_error("bad run in snapshot");
    for (size_t k = 0; k < run; ++k)
      set(cells[i + k], v);
    i += run;
  }
}

#define WHAS_CELL_COLUMNS(X)                                                   \
  X(uint8_t, static_cast<uint8_t>(c.element), c.element = static_cast<Element>(v)) \
  X(uint8_t, c.flags, c.flags = v)                                             \
  X(float, c.temperature, c.temperature = v)                                   \
  X(float, c.pressure, c.pressure = v)                                         \
  X(float, c.mass, c.mass = v)                                                 \
  X(float, c.density, c.density = v)                                           \
  X(float, c.vx, c.vx = v)                                                     \
  X(float, c.vy, c.vy = v)                                                     \
  X(float, c.inertia, c.inertia = v)                                           \
  X(uint32_t, c.lastUpdateFrame, c.lastUpdateFrame = v)                        \
  X(float, c.lifetime, c.lifetime = v)                                         \
  X(float, c.hardness, c.hardness = v)                                         \
  X(float, c.moisture, c.moisture = v)

void PutStats(ByteWriter &out, const SpellStats &s) {
  out.Put(s.valid);
  out.Put(static_cast<uint8_t>(s.kind));
  out.Put(static_cast<uint8_t>(s.element));
  out.Put(s.netLocal.x);
  out.Put(s.netLocal.y);
  for (float v : {s.totalMagnitude, s.imbalance, s.offsetRad, s.speed, s.range,
                  s.density, s.power, s.diameter})
    out.Put(v);
  out.Put(s.particleCount);
  for (float v : {s.temperature, s.launchSpeed, s.force, s.duration})
    out.Put(v);
  // Effects are always single parts, so `parts` is never stored
  out.Put(static_cast<uint8_t>(s.shape));
  for (float v : {s.temperatureDelta, s.hardnessScale, s.crush, s.restore,
                  s.collectRadius})
    out.Put(v);
  out.Put(s.collectMax);
}

SpellStats GetStats(ByteReader &in) {
  SpellStats s;
  s.valid = in.Get<bool>();
  s.kind = static_cast<SpellKind>(in.Get<uint8_t>());
  s.element = static_cast<Element>(in.Get<uint8_t>());
  s.netLocal.x = in.Get<float>();
  s.netLocal.y = in.Get<float>();
  for (float *v : {&s.totalMagnitude, &s.imbalance, &s.offsetRad, &s.speed,
                   &s.range, &s.density, &s.power, &s.diameter})
    *v = in.Get<float>();
  s.particleCount = in.Get<int>();
  for (float *v : {&s.temperature, &s.launchSpeed, &s.force, &s.duration})
    *v = in.Get<float>();
  s.shape = static_cast<SpellShape>(in.Get<uint8_t>());
  for (float *v : {&s.temperatureDelta, &s.hardnessScale, &s.crush,
                   &s.restore, &s.collectRadius})
    *v = in.Get<float>();
  s.collectMax = in.Get<int>();
  return s;
}

} // namespace

std::vector<uint8_t> Simulation::SaveSnapshot() const {
  ByteWriter out;
  out.Put(MAGIC);
  out.Put(m_frameCounter);
  out.Put(m_seed);
  out.Put(m_rng.State());

  const auto &cells = m_grid.GetBuffer();
  out.Put(static_cast<uint32_t>(cells.size()));
#define PUT(T, get, set) PutColumn<T>(out, cells, [](const Cell &c) { return get; });
  WHAS_CELL_COLUMNS(PUT)
#undef PUT

  const auto &pressure = const_cast<Grid &>(m_grid).GetPressureBuffer();
  out.Put(static_cast<uint32_t>(pressure.size()));
  for (size_t i = 0; i < pressure.size();) {
    uint16_t run = 1;
    while (i + run < pressure.size() && run < MAX_RUN &&
           std::memcmp(&pressure[i + run], &pressure[i], sizeof(float)) == 0)
      ++run;
    out.Put(run);
    out.Put(pressure[i]);
    i += run;
  }

  const auto &chunks = m_chunks.GetChunks();
  out.Put(static_cast<uint32_t>(chunks.size()));
  for (const Chunk &c : chunks) {
    out.Put(c.active);
    out.Put(c.wakeNextFrame);
    out.Put(c.activeCount);
    out.Put(c.lastChangeFrame);
    out.Put(c.lastStaticChangeFrame);
  }

  // Only live particles; the pool is refilled from the front on load
  uint32_t live = 0;
  for (const Particle &p : m_particles.Pool())
    live += p.active;
  out.Put(live);
  for (const Particle &p : m_particles.Pool()) {
    if (!p.active)
      continue;
    out.Put(p.pos.x);
    out.Put(p.pos.y);
    out.Put(p.vel.x);
    out.Put(p.vel.y);
    out.Put(static_cast<uint8_t>(p.element));
    out.Put(p.isProjectile);
    out.Put(p.remainingDistance);
    out.Put(p.power);
    out.Put(p.owner);
    out.Put(p.temperature);
    out.Put(p.temperatureDelta);
    out.Put(p.hardnessScale);
    out.Put(p.crush);
    out.Put(p.restore);
  }

  out.Put(static_cast<uint32_t>(m_activeSpellEffects.size()));
  for (const SpellEffect &e : m_activeSpellEffects) {
    PutStats(out, e.stats);
    out.Put(e.origin.x);
    out.Put(e.origin.y);
    out.Put(e.direction.x);
    out.Put(e.direction.y);
    out.Put(e.emitted);
    out.Put(e.owner);
    out.Put(e.timeRemaining);
    out.Put(e.shapePart);
    out.Put(e.rows);
    out.Put(e.partRows);
    out.Put(e.bonusParticles);
  }
  return std::move(out.Data());
}

bool Simulation::LoadSnapshot(const std::vector<uint8_t> &data) {
  try {
    ByteReader in(data);
    if (in.Get<uint32_t>() != MAGIC)
      return false;
    uint32_t frame = in.Get<uint32_t>();
    uint64_t seed = in.Get<uint64_t>();
    uint64_t rngState = in.Get<uint64_t>();

    auto &cells = m_grid.GetBuffer();
    if (in.Get<uint32_t>() != cells.size())
      return false;
    std::vector<Cell> newCells(cells.size());
#define GET(T, get, set) GetColumn<T>(in, newCells, [](Cell &c, T v) { set; });
    WHAS_CELL_COLUMNS(GET)
#undef GET
    for (Cell &c : newCells) {
      if (c.element >= Element::COUNT)
        return false;
      c.bodyID = -1;
      c.triangleID = -1;
      c.isStatic = false;
    }

    auto &pressure = m_grid.GetPressureBuffer();
    if (in.Get<uint32_t>() != pressure.size())
      return false;
    std::vector<float> newPressure;
    newPressure.reserve(pressure.size());
    while (newPressure.size() < pressure.size()) {
      uint16_t run = in.Get<uint16_t>();
      float v = in.Get<float>();
      if (run == 0 || newPressure.size() + run > pressure.size())
        return false;
      newPressure.insert(newPressure.end(), run, v);
    }

    auto &chunks = m_chunks.GetChunks();
    if (in.Get<uint32_t>() != chunks.size())
      return false;
    std::vector<Chunk> newChunks(chunks.size());
    for (Chunk &c : newChunks) {
      c.active = in.Get<bool>();
      c.wakeNextFrame = in.Get<bool>();
      c.activeCount = in.Get<int>();
      c.lastChangeFrame = in.Get<uint32_t>();
      c.lastStaticChangeFrame = in.Get<uint32_t>();
    }

    auto &pool = m_particles.Pool();
    uint32_t live = in.Get<uint32_t>();
    if (live > pool.size())
      return false;
    std::vector<Particle> newPool(pool.size());
    for (uint32_t i = 0; i < live; ++i) {
      Particle &p = newPool[i];
      p.active = true;
      p.pos.x = in.Get<float>();
      p.pos.y = in.Get<float>();
      p.vel.x = in.Get<float>();
      p.vel.y = in.Get<float>();
      p.element = static_cast<Element>(in.Get<uint8_t>());
      p.isProjectile = in.Get<bool>();
      p.remainingDistance = in.Get<float>();
      p.power = in.Get<float>();
      p.owner = in.Get<int>();
      p.temperature = in.Get<float>();
      p.temperatureDelta = in.Get<float>();
      p.hardnessScale = in.Get<float>();
      p.crush = in.Get<float>();
      p.restore = in.Get<float>();
    }

    uint32_t effects = in.Get<uint32_t>();
    if (effects > 4096)
      return false;
    std::vector<SpellEffect> newEffects(effects);
    for (SpellEffect &e : newEffects) {
      e.stats = GetStats(in);
      e.origin.x = in.Get<float>();
      e.origin.y = in.Get<float>();
      e.direction.x = in.Get<float>();
      e.direction.y = in.Get<float>();
      e.emitted = in.Get<int>();
      e.owner = in.Get<int>();
      e.timeRemaining = in.Get<float>();
      e.shapePart = in.Get<uint8_t>();
      e.rows = in.Get<int>();
      e.partRows = in.Get<int>();
      e.bonusParticles = in.Get<int>();
    }
    if (!in.Done())
      return false;

    // Everything parsed: apply
    m_rigidBodies.Reset();
    cells = std::move(newCells);
    pressure = std::move(newPressure);
    chunks = std::move(newChunks);
    pool = std::move(newPool);
    m_particles.TakeHits();
    m_activeSpellEffects = std::move(newEffects);
    m_frameCounter = frame;
    m_seed = seed;
    m_rng = DetRng::FromState(rngState);
    for (auto &spawns : m_chunkSpawns)
      spawns.clear();
    return true;
  } catch (const std::runtime_error &) {
    return false;
  }
}
