#include "whas/physics/rigid_body_system.h"
#include "raylib.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/geometry_utils.h"
#include "whas/physics/particle_system.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace {

inline int32_t MakeBodyID(b2BodyId id) {
  return (static_cast<int32_t>(id.index1) << 16) |
         (static_cast<int32_t>(id.generation) & 0xFFFF);
}

// FIX 1 — DEFORMATION:
// The old code used floor(lx + 0.5f) (i.e. round-to-nearest in local space)
// and then iterated over the world-space bounding box of the rotated AABB.
// This causes two problems:
//   a) Multiple world pixels can map to the same local pixel (overdraw) while
//      adjacent local pixels map to *no* world pixel (holes) — producing the
//      "crumbling sphere" look during rotation.
//   b) Clearing old pixels in PostUpdate and re-drawing them in SyncBackToGrid
//      using the same imprecise mapping leaves orphan cells.
//
// Fix: iterate in LOCAL pixel space and project each pixel forward into world
// space, then stamp exactly one world cell per local pixel. This is a
// forward-mapping approach: for every (lx, ly) in the body's pixel list we
// compute the world (wx, wy) and write there. No bounding-box sweep, no
// rounding ambiguity, no holes.
//
// We still need a *clear* pass before the step. For clearing we use the
// bodyID tag — every cell that has our bodyID gets wiped regardless of
// where it ended up last frame. This is safe because we wrote the tag on
// the previous SyncBack.

inline void ProjectToWorld(float lx, float ly, b2Vec2 pos, b2Rot rot, int &wx,
                           int &wy) {
  wx = static_cast<int>(std::round(pos.x + lx * rot.c - ly * rot.s));
  wy = static_cast<int>(std::round(pos.y + lx * rot.s + ly * rot.c));
}

} // namespace

RigidBodySystem::RigidBodySystem() {
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = {0.0f, 9.8f};
  m_worldId = b2CreateWorld(&worldDef);
}

RigidBodySystem::~RigidBodySystem() { b2DestroyWorld(m_worldId); }

// ---------------------------------------------------------------------------
// PreUpdate — damage detection & body release
// ---------------------------------------------------------------------------
void RigidBodySystem::PreUpdate(Grid &grid, ElementContext &ctx) {
  static int32_t nextReleaseID = -2;

  for (auto it = m_bodies.begin(); it != m_bodies.end();) {
    auto &bodyData = *it;
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    bool damaged = false;
    for (auto &p : bodyData.originalPixels) {
      int tx, ty;
      ProjectToWorld(p.first, p.second, pos, rot, tx, ty);
      if (grid.InBounds(tx, ty)) {
        Cell &c = grid.Get(tx, ty);
        // Negative bodyID means AIR was painted here or another body released
        if (c.bodyID < 0) {
          damaged = true;
          break;
        }
      }
    }

    if (damaged) {
      // Give released pixels a unique temporary ID so ExtractDynamicBodies
      // won't re-merge them with pixels from a different breakage event.
      int32_t releaseID = nextReleaseID--;
      if (nextReleaseID > -2)
        nextReleaseID = -2;

      for (auto &p : bodyData.originalPixels) {
        int tx, ty;
        ProjectToWorld(p.first, p.second, pos, rot, tx, ty);
        if (grid.InBounds(tx, ty)) {
          Cell &c = grid.Get(tx, ty);
          if (c.bodyID == selfID) {
            c.bodyID = releaseID;
          }
        }
      }
      b2DestroyBody(bodyData.bodyId);
      it = m_bodies.erase(it);
      continue;
    }
    ++it;
  }
}

// ---------------------------------------------------------------------------
// PostUpdate — clear → mesh → step → displace → sync
// ---------------------------------------------------------------------------
void RigidBodySystem::PostUpdate(Grid &grid, ElementContext &ctx,
                                 ParticleSystem &particles, SimulationConfig &config, float dt) {
  // 1. Clear old body pixels from grid using forward-mapping (FIX 1 — no holes)
  ClearBodiesFromGrid(grid, config);

  // 2. Rebuild static world meshes (only for dirty chunks — FIX 2)
  UpdateWorldMeshes(grid, ctx);

  // 3. Step physics
  b2World_Step(m_worldId, 1.0f / 60.0f, 4);

  // 4. Fluid displacement & drag
  ProcessDisplacement(grid, ctx, particles);

  // 5. Write bodies back to grid
  SyncBackToGrid(grid, ctx);
}

// ---------------------------------------------------------------------------
// ClearBodiesFromGrid — FIX 1: forward-map each local pixel to world space
// ---------------------------------------------------------------------------
void RigidBodySystem::ClearBodiesFromGrid(Grid &grid, SimulationConfig &config) {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    for (auto &p : bodyData.originalPixels) {
      int wx, wy;
      ProjectToWorld(p.first, p.second, pos, rot, wx, wy);
      if (grid.InBounds(wx, wy)) {
        Cell &c = grid.Get(wx, wy);
        if (c.bodyID == selfID) {
          c = ElementFactory::Create(Element::AIR, config);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// UpdateWorldMeshes — FIX 2: only regen chunks whose lastStaticChangeFrame
// has advanced; skip the rest entirely instead of checking every chunk.
// ---------------------------------------------------------------------------
void RigidBodySystem::UpdateWorldMeshes(Grid &grid, ElementContext &ctx) {
  for (int cy = 0; cy < CHUNK_ROWS; ++cy) {
    for (int cx = 0; cx < CHUNK_COLS; ++cx) {
      auto &chunk = ctx.chunks.GetChunk(cx, cy);
      int chunkIdx = cy * CHUNK_COLS + cx;

      // Early-out: nothing changed since last build
      auto it = m_chunkMeshes.find(chunkIdx);
      if (it != m_chunkMeshes.end() &&
          it->second.lastChangeFrame >= chunk.lastStaticChangeFrame) {
        continue; // <-- this is the key skip that eliminates the per-frame
                  // rebuild
      }

      // Destroy old body if any
      if (it != m_chunkMeshes.end()) {
        b2DestroyBody(it->second.bodyId);
        m_chunkMeshes.erase(it);
      }

      int x0 = cx * CHUNK_SIZE, y0 = cy * CHUNK_SIZE;
      std::vector<bool> mask(CHUNK_SIZE * CHUNK_SIZE, false);
      bool hasSolid = false;

      for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
          if (grid.InBounds(x0 + lx, y0 + ly)) {
            const Cell &c = grid.Get(x0 + lx, y0 + ly);
            if (c.element == Element::EARTH) {
              mask[ly * CHUNK_SIZE + lx] = true;
              hasSolid = true;
            }
          }
        }
      }

      if (hasSolid) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {(float)x0, (float)y0};
        b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

        const auto &props =
            ctx.config.elements[static_cast<size_t>(Element::EARTH)];
        AddTriangulatedShapes(bodyId, mask, CHUNK_SIZE, CHUNK_SIZE, 0, 0,
                              props.density, props.restitution);
        m_chunkMeshes[chunkIdx] = {bodyId, chunk.lastStaticChangeFrame};
      }
      // If no solid, just leave the entry absent — no body needed
    }
  }
}

// ---------------------------------------------------------------------------
// ProcessDisplacement — unchanged logic, but uses forward-mapping (FIX 1)
// ---------------------------------------------------------------------------
void RigidBodySystem::ProcessDisplacement(Grid &grid, ElementContext &ctx,
                                          ParticleSystem &particles) {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    b2Vec2 vel = b2Body_GetLinearVelocity(bodyData.bodyId);
    float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    float dragForce = 0.0f;

    int maskW = bodyData.maxX - bodyData.minX + 1;

    for (auto &p : bodyData.originalPixels) {
      int wx, wy;
      ProjectToWorld(p.first, p.second, pos, rot, wx, wy);
      if (!grid.InBounds(wx, wy))
        continue;

      Cell &c = grid.Get(wx, wy);
      if (c.bodyID != -1 || c.element == Element::AIR)
        continue;

      const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];

      c.vx += vel.x * 0.2f;
      c.vy += vel.y * 0.2f;
      ctx.chunks.WakeChunkAt(wx, wy, ctx.frameIndex);

      if (speed > 4.0f) {
        float splashChance = (speed - 4.0f) * 0.1f;
        if ((float)(ctx.rng() % 100) / 100.0f < splashChance) {
          Vector2 pVel = {vel.x * 0.4f +
                              (float)((ctx.rng() % 100) - 50) * 0.05f,
                          vel.y * 0.4f - (float)(ctx.rng() % 50) * 0.1f};
          particles.Spawn({(float)wx, (float)wy}, pVel, c.element);
          c = ElementFactory::Create(Element::AIR, ctx.config);
        }
      }

      dragForce += props.density * 0.0005f;
    }

    if (dragForce > 0.0f && speed > 0.1f) {
      b2Vec2 drag = {-vel.x / speed * dragForce, -vel.y / speed * dragForce};
      b2Body_ApplyForceToCenter(bodyData.bodyId, drag, true);
    }
  }
}

// ---------------------------------------------------------------------------
// SyncBackToGrid — FIX 1: forward-map to avoid holes/deformation
// ---------------------------------------------------------------------------
void RigidBodySystem::SyncBackToGrid(Grid &grid, ElementContext &ctx) {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);
    int maskW = bodyData.maxX - bodyData.minX + 1;

    for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
      auto &p = bodyData.originalPixels[i];
      int wx, wy;
      ProjectToWorld(p.first, p.second, pos, rot, wx, wy);
      if (!grid.InBounds(wx, wy))
        continue;

      Cell &c = grid.Get(wx, wy);

      if (c.bodyID == -1 && c.element != Element::AIR) {
        ctx.chunks.WakeChunkAt(wx, wy, ctx.frameIndex);
      }

      if (c.bodyID < 0 || c.bodyID == selfID) {
        int lx = (int)std::round(p.first) - bodyData.minX;
        int ly = (int)std::round(p.second) - bodyData.minY;
        int idx = ly * maskW + lx;
        c.element = bodyData.localElements[idx];
        c.bodyID = selfID;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// ExtractDynamicBodies — FIX 3: ALL orphaned rigid pixels become particles,
// regardless of blob size. The old code only did this for blobs < 10 pixels
// and re-extracted larger ones, leaving visible rock clumps on detach.
// ---------------------------------------------------------------------------
void RigidBodySystem::ExtractDynamicBodies(Grid &grid, ElementContext &ctx,
                                           ParticleSystem &particles) {
  std::vector<bool> visited(GRID_W * GRID_H, false);

  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      int idx = y * GRID_W + x;
      if (visited[idx])
        continue;

      Cell &cell = grid.Get(x, y);
      const auto &props =
          ctx.config.elements[static_cast<size_t>(cell.element)];

      if (!props.rigidBody)
        continue;

      // bodyID == -1 → never-been-a-body rigid pixel (freshly painted)
      // bodyID < -1  → released/orphaned pixel from a broken body
      const bool isFresh = (cell.bodyID == -1);
      const bool isOrphan = (cell.bodyID < -1);

      if (!isFresh && !isOrphan)
        continue;

      // BFS flood-fill to find the connected blob
      std::vector<std::pair<int, int>> pixels;
      std::queue<std::pair<int, int>> q;
      q.push({x, y});
      visited[idx] = true;
      float sumX = 0, sumY = 0;
      Element startElement = cell.element;
      int32_t startBodyID = cell.bodyID;

      while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        pixels.push_back({cx, cy});
        sumX += cx;
        sumY += cy;

        const int dx[] = {0, 0, 1, -1};
        const int dy[] = {1, -1, 0, 0};
        for (int i = 0; i < 4; ++i) {
          int nx = cx + dx[i], ny = cy + dy[i];
          if (!grid.InBounds(nx, ny))
            continue;
          int nidx = ny * GRID_W + nx;
          if (visited[nidx])
            continue;
          Cell &nc = grid.Get(nx, ny);
          if (nc.element == startElement && nc.bodyID == startBodyID) {
            visited[nidx] = true;
            q.push({nx, ny});
          }
        }
      }

      // FIX 3: Orphaned blobs (released from a broken body) ALWAYS become
      // particles and disappear — no re-extraction into a new rigid body.
      // This eliminates the lingering rock clumps.
      if (isOrphan) {
        for (auto &p : pixels) {
          Cell &c = grid.Get(p.first, p.second);
          Vector2 pVel = {(float)((ctx.rng() % 100) - 50) * 0.1f,
                          (float)((ctx.rng() % 100) - 50) * 0.1f};
          particles.Spawn({(float)p.first, (float)p.second}, pVel, c.element);
          c = ElementFactory::Create(Element::AIR, ctx.config);
        }
        continue;
      }

      // Fresh pixels (isFresh): small blobs crumble to particles,
      // large blobs become a new rigid body — same as before.
      if (pixels.size() < 10) {
        for (auto &p : pixels) {
          Cell &c = grid.Get(p.first, p.second);
          Vector2 pVel = {(float)((ctx.rng() % 100) - 50) * 0.1f,
                          (float)((ctx.rng() % 100) - 50) * 0.1f};
          particles.Spawn({(float)p.first, (float)p.second}, pVel, c.element);
          c = ElementFactory::Create(Element::AIR, ctx.config);
        }
        continue;
      }

      float centerX = sumX / pixels.size(), centerY = sumY / pixels.size();

      b2BodyDef bodyDef = b2DefaultBodyDef();
      bodyDef.type = b2_dynamicBody;
      bodyDef.position = {centerX, centerY};
      b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

      int minX = pixels[0].first, maxX = minX;
      int minY = pixels[0].second, maxY = minY;
      for (auto &p : pixels) {
        minX = std::min(minX, p.first);
        maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second);
        maxY = std::max(maxY, p.second);
      }
      int width = maxX - minX + 1, height = maxY - minY + 1;
      std::vector<bool> mask(width * height, false);
      for (auto &p : pixels)
        mask[(p.second - minY) * width + (p.first - minX)] = true;

      AddTriangulatedShapes(bodyId, mask, width, height, centerX - minX,
                            centerY - minY, props.density, props.restitution);

      BodyData data;
      data.bodyId = bodyId;
      data.minX = (int)std::floor(minX - centerX);
      data.maxX = (int)std::ceil(maxX - centerX);
      data.minY = (int)std::floor(minY - centerY);
      data.maxY = (int)std::ceil(maxY - centerY);
      int dw = data.maxX - data.minX + 1;
      int dh = data.maxY - data.minY + 1;
      data.pixelMask.assign(dw * dh, false);
      data.localElements.assign(dw * dh, Element::AIR);

      for (auto &p : pixels) {
        int lx = (int)std::round(p.first - centerX);
        int ly = (int)std::round(p.second - centerY);
        int kidx = (ly - data.minY) * dw + (lx - data.minX);
        data.pixelMask[kidx] = true;
        data.localElements[kidx] = startElement;
        data.elements.push_back(startElement);
        data.originalPixels.push_back({(float)lx, (float)ly});
        grid.Get(p.first, p.second).bodyID = MakeBodyID(bodyId);
      }
      m_bodies.push_back(data);
    }
  }
}

// ---------------------------------------------------------------------------
// AddTriangulatedShapes — unchanged
// ---------------------------------------------------------------------------
void RigidBodySystem::AddTriangulatedShapes(b2BodyId bodyId,
                                            const std::vector<bool> &mask,
                                            int width, int height, float offX,
                                            float offY, float density,
                                            float restitution) {
  auto loops = GeometryUtils::MarchingSquares(mask, width, height);
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = density / 1000.0f;

  for (auto &loop : loops) {
    auto simplified = GeometryUtils::DouglasPeucker(loop, 0.5f);
    auto triangles = GeometryUtils::Triangulate(simplified);
    for (auto &tri : triangles) {
      b2Vec2 points[3];
      for (int i = 0; i < 3; ++i)
        points[i] = {tri[i].x - offX, tri[i].y - offY};

      b2Hull hull = b2ComputeHull(points, 3);
      if (hull.count == 3) {
        b2Polygon poly = b2MakePolygon(&hull, 0.0f);
        b2CreatePolygonShape(bodyId, &shapeDef, &poly);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// DrawDebug — unchanged
// ---------------------------------------------------------------------------
void RigidBodySystem::DrawDebug() {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    int shapeCount = b2Body_GetShapeCount(bodyData.bodyId);
    if (shapeCount > 0) {
      std::vector<b2ShapeId> shapes(shapeCount);
      b2Body_GetShapes(bodyData.bodyId, shapes.data(), shapeCount);
      for (auto shapeId : shapes) {
        b2Polygon poly = b2Shape_GetPolygon(shapeId);
        for (int i = 0; i < poly.count; ++i) {
          int next = (i + 1) % poly.count;
          b2Vec2 p1 = b2TransformPoint({pos, rot}, poly.vertices[i]);
          b2Vec2 p2 = b2TransformPoint({pos, rot}, poly.vertices[next]);
          DrawLine((int)(p1.x * CELL_SIZE), (int)(p1.y * CELL_SIZE),
                   (int)(p2.x * CELL_SIZE), (int)(p2.y * CELL_SIZE), RED);
        }
      }
    }
  }
}
