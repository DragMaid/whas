#pragma once
#include "whas/core/cell.h"
#include "whas/core/config.h"
#include "whas/core/element.h"
#include "whas/world/chunk_manager.h"
#include "whas/world/grid.h"
#include "whas/physics/rigid_body_system.h"
#include "whas/physics/particle_system.h"
#include <random>
#include <thread>
#include <vector>
#include <barrier>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>

struct ElementContext;

class Simulation {
public:
  Simulation();
  ~Simulation();

  void Update(float dt, bool isPainting = false);

  void Paint(int cx, int cy, Element element, int brushRadius);
  void Erase(int cx, int cy, int brushRadius);

  const Cell &GetCell(int x, int y) const { return m_grid.Get(x, y); }

  int GetActiveChunks() const { return m_chunks.GetActiveChunksCount(); }
  int GetParticleCount() const { return m_particleCount; }
  float GetAvgPressure() const { return m_avgPressure; }
  float GetAvgTemp() const { return m_avgTemp; }

  // Runtime Tuning
  SimulationConfig &GetConfig() { return m_config; }
  RigidBodySystem &GetRigidBodySystem() { return m_rigidBodies; }
  ParticleSystem &GetParticleSystem() { return m_particles; }

private:
  SimulationConfig m_config;      // Editable source
  SimulationConfig m_frameConfig; // Per-frame snapshot
  uint32_t m_frameCounter = 0;

  std::mt19937 m_rng;

  Grid m_grid;
  ChunkManager m_chunks;
  RigidBodySystem m_rigidBodies;
  ParticleSystem m_particles;

  int m_particleCount = 0;
  float m_avgPressure = 0.0f;
  float m_avgTemp = 0.0f;

  void UpdateElements();
  void UpdatePhysics(float dt, bool isPainting);
  void CollectStatistics();

  // Parallel Workers
  std::vector<std::jthread> m_workers;
  std::barrier<std::function<void()>> m_syncBarrier;
  std::atomic<bool> m_running{true};
  std::atomic<int> m_currentPass{0};
  float m_lastDt = 0.0f;
  
  std::mutex m_wakeMutex;
  std::condition_variable m_wakeCv;
  std::atomic<uint32_t> m_workerFrame{0};

  void WorkerLoop(int threadIdx, std::stop_token stopToken);
  void UpdateChunk(int chunkIdx, ElementContext &ctx);
};
