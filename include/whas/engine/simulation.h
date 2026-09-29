#pragma once
#include "whas/constants.h"
#include "whas/core/cell.h"
#include "whas/core/config.h"
#include "whas/core/element.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include "whas/physics/rigid_body_system.h"
#include "whas/physics/particle_system.h"
#include "whas/spell/spell_types.h"
#include "whas/spell/spell_system.h"
#include "whas/core/det_rng.h"
#include <thread>
#include <cstdint>
#include <vector>
#include <barrier>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>

struct ElementContext;

class Simulation {
public:
  // Worker count is fixed rather than taken from the hardware; results don't
  // depend on it, but it keeps performance the same on every machine
  explicit Simulation(int workerThreads = SIM_THREADS);
  ~Simulation();

  // Restart the random streams. Both lockstep clients use the match seed.
  void SetSeed(uint64_t seed);
  uint64_t GetSeed() const { return m_seed; }

  // Hash of everything that affects future ticks: cells, particles, spell
  // effects and rigid bodies. Lockstep clients compare it after each turn.
  uint64_t StateHash() const;

  // Everything StateHash covers except rigid bodies, which can't be copied
  // out of Box2D: loading drops them and they are rebuilt from the cells.
  // A desynced client and the reference both load the same snapshot, so
  // they are identical again afterwards.
  std::vector<uint8_t> SaveSnapshot() const;
  bool LoadSnapshot(const std::vector<uint8_t> &data);

  void Update(float dt, bool isPainting = false);

  void Paint(int cx, int cy, Element element, int brushRadius);
  void Erase(int cx, int cy, int brushRadius);
  // Empty the world: every cell, particle and in-flight spell
  void Reset();
  // A brand-new world regardless of history: frame counter, chunks, rigid
  // bodies and random streams all start over. Every match round starts here
  // so peers with different pasts (one came from the sandbox) agree.
  void Restart(uint64_t seed);

  const Cell &GetCell(int x, int y) const { return m_grid.Get(x, y); }

  int GetActiveChunks() const { return m_chunks.GetActiveChunksCount(); }
  int GetParticleCount() const { return m_particleCount; }
  float GetAvgPressure() const { return m_avgPressure; }
  float GetAvgTemp() const { return m_avgTemp; }

  // Runtime Tuning
  SimulationConfig &GetConfig() { return m_config; }
  const SimulationConfig &GetConfig() const { return m_config; }
  RigidBodySystem &GetRigidBodySystem() { return m_rigidBodies; }
  ParticleSystem &GetParticleSystem() { return m_particles; }
  const std::vector<SpellEffect> &GetActiveSpellEffects() const {
    return m_activeSpellEffects;
  }

  // Spell casting; owner is the caster's hurtbox id so it can't hit itself
  void CastSpell(const Spell &spell, Vector2 origin, Vector2 aimDirection,
                 int owner = -1);
  // Cast with stats handed in (lockstep play uses the server's stats)
  void CastSpell(const SpellStats &stats, Vector2 origin, Vector2 aimDirection,
                 int owner = -1);

private:
  // Collection: draw matching cells around the caster into the spell,
  // returns how many
  int Collect(const SpellStats &stats, Vector2 origin);

  SimulationConfig m_config;      // Editable source
  SimulationConfig m_frameConfig; // Per-frame snapshot
  uint32_t m_frameCounter = 0;

  uint64_t m_seed = 42;
  DetRng m_rng; // main-thread stream
  // Particles spawned by workers, per chunk, flushed in chunk order
  std::vector<std::vector<PendingSpawn>> m_chunkSpawns;

  Grid m_grid;
  ChunkManager m_chunks;
  RigidBodySystem m_rigidBodies;
  ParticleSystem m_particles;
  std::vector<SpellEffect> m_activeSpellEffects;

  int m_particleCount = 0;
  float m_avgPressure = 0.0f;
  float m_avgTemp = 0.0f;

  void UpdateElements();
  void UpdatePhysics(float dt, bool isPainting);
  void CollectStatistics();

  // Parallel Workers
  int m_numThreads;
  std::vector<std::jthread> m_workers;
  std::barrier<std::function<void()>> m_syncBarrier;
  std::atomic<bool> m_running{true};
  std::atomic<int> m_currentPass{0};
  float m_lastDt = 0.0f;
  
  std::mutex m_wakeMutex;
  std::condition_variable m_wakeCv;
  std::atomic<uint32_t> m_workerFrame{0};
  // Counts every Update, even across Restart (which sends the frame counter
  // back to 0), so workers always see new work
  std::atomic<uint64_t> m_workGeneration{0};

  void WorkerLoop(int threadIdx, std::stop_token stopToken);
  void UpdateChunk(int chunkIdx, const ElementContext &base);
  void FlushWorkerSpawns();
};
