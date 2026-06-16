#include "whas/engine/simulation.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/element/base/registry.h"
#include "whas/physics/heat_system.h"
#include "whas/physics/pressure_system.h"
#include <algorithm>

Simulation::Simulation()
    : m_config(), m_frameConfig(), m_rng(42), m_grid(m_config), m_particles(),
      m_syncBarrier(std::thread::hardware_concurrency() + 1, [this]() {
        m_currentPass++;
      }) {
  int numThreads = std::thread::hardware_concurrency();
  for (int i = 0; i < numThreads; ++i) {
    m_workers.emplace_back([this, i](std::stop_token st) { WorkerLoop(i, st); });
  }
}

Simulation::~Simulation() {
  m_running = false;
  m_wakeCv.notify_all();
}

void Simulation::Update(float dt, bool isPainting) {
  m_lastDt = dt;
  m_frameCounter++;
  m_chunks.BeginFrame();
  m_frameConfig = m_config;

  ElementContext ctx{m_grid, m_chunks, m_rng, m_frameConfig, m_frameCounter};
  
  // 1. Inject RigidBody pixels into grid before simulation
  m_rigidBodies.PreUpdate(m_grid, ctx);

  PressureSystem::Update(ctx);

  m_workerFrame = m_frameCounter;
  m_wakeCv.notify_all();

  // Wait for worker threads to finish falling sand simulation
  for (int p = 0; p < 5; ++p) {
    m_syncBarrier.arrive_and_wait();
  }

  // 2. Extract and Step Physics (Post simulation)
  if (!isPainting) {
    m_rigidBodies.ExtractDynamicBodies(m_grid, ctx);
  }
  m_rigidBodies.PostUpdate(m_grid, ctx, m_particles, dt);

  // 3. Update Particles
  m_particles.Update(m_grid, ctx, dt);

  // Heat and Pressure propagation
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
      m_wakeCv.wait(lock, [&] { return m_workerFrame > lastFrame || !m_running; });
    }
    if (!m_running) break;

    uint32_t currentFrame = m_workerFrame;
    std::mt19937 threadRng(currentFrame + threadIdx);
    ElementContext ctx{m_grid, m_chunks, threadRng, m_frameConfig, currentFrame};

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

      if (c.lastUpdateFrame == ctx.frameIndex) continue;
      if (c.element == Element::AIR) continue;

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

void Simulation::UpdateElements() {
}

void Simulation::Paint(int cx, int cy, Element element, int brushRadius) {

  for (int dy = -brushRadius; dy <= brushRadius; ++dy) {
    for (int dx = -brushRadius; dx <= brushRadius; ++dx) {
      if (dx * dx + dy * dy > brushRadius * brushRadius)
        continue;

      int x = cx + dx;
      int y = cy + dy;

      if (!m_grid.InBounds(x, y))
        continue;

      Cell c = ElementFactory::Create(element, m_config);
      m_grid.Get(x, y) = c;
      m_chunks.WakeChunkAt(x, y, m_frameCounter);
    }
  }
}

void Simulation::Erase(int cx, int cy, int brushRadius) {
  Paint(cx, cy, Element::AIR, brushRadius);
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
