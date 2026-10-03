#include <cstring>
#include "whas/physics/rigid_body_system.h"
#include "raylib.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include "whas/physics/geometry_utils.h"
#include "whas/physics/movement_system.h"
#include "whas/physics/particle_system.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

namespace {

// Create a unique ID by padding and combining 2 library native IDs
inline int32_t MakeBodyID(b2BodyId id) {
  return (static_cast<int32_t>(id.index1) << 16) |
         (static_cast<int32_t>(id.generation) & 0xFFFF);
}

// Return game world position projections from Box2D world
inline std::pair<int, int> ProjectToWorld(float lx, float ly, b2Vec2 pos,
                                          b2Rot rot) {
  int wx = static_cast<int>(std::round(pos.x + lx * rot.c - ly * rot.s));
  int wy = static_cast<int>(std::round(pos.y + lx * rot.s + ly * rot.c));
  return {wx, wy};
}

} // namespace

// Constructor
RigidBodySystem::RigidBodySystem() { CreateWorld(); }

void RigidBodySystem::Reset() {
  b2DestroyWorld(m_worldId); // takes every body and mesh with it
  m_bodies.clear();
  m_chunkMeshes.clear();
  CreateWorld();
}

void RigidBodySystem::CreateWorld() {
  b2WorldDef worldDef = b2DefaultWorldDef();
  // TODO: move this to config
  worldDef.gravity = {0.0f, 9.8f};
  m_worldId = b2CreateWorld(&worldDef);
  m_chunkMeshes.resize(CHUNK_COLS * CHUNK_ROWS);

  // Walls around the grid so bodies can't leave the world (and vanish)
  b2BodyDef boundsDef = b2DefaultBodyDef();
  boundsDef.type = b2_staticBody;
  b2BodyId bounds = b2CreateBody(m_worldId, &boundsDef);
  b2ShapeDef wallDef = b2DefaultShapeDef();
  constexpr float t = 2.0f; // wall half-thickness, in cells
  const float w = static_cast<float>(GRID_W);
  const float h = static_cast<float>(GRID_H);
  const b2Polygon walls[] = {
      b2MakeOffsetBox(w * 0.5f + t, t, {w * 0.5f, -t}, b2Rot_identity),
      b2MakeOffsetBox(w * 0.5f + t, t, {w * 0.5f, h + t}, b2Rot_identity),
      b2MakeOffsetBox(t, h * 0.5f + t, {-t, h * 0.5f}, b2Rot_identity),
      b2MakeOffsetBox(t, h * 0.5f + t, {w + t, h * 0.5f}, b2Rot_identity),
  };
  for (const b2Polygon &wall : walls)
    b2CreatePolygonShape(bounds, &wallDef, &wall);
}

// Destructor
RigidBodySystem::~RigidBodySystem() {
  for (auto &mesh : m_chunkMeshes) {
    if (mesh.active) {
      b2DestroyBody(mesh.bodyId);
    }
  }
  b2DestroyWorld(m_worldId);
}

void RigidBodySystem::PreUpdate(Grid &grid, ElementContext &ctx) {
  // Static variable remember its value between frames
  // Represent temporary groups of pixels
  static int32_t nextReleaseID = -2;

  // Get all the body data and project them to world
  for (auto it = m_bodies.begin(); it != m_bodies.end();) {
    BodyData &bd = *it;
    b2Vec2 pos = b2Body_GetPosition(bd.bodyId);
    b2Rot rot = b2Body_GetRotation(bd.bodyId);
    const int32_t selfID = MakeBodyID(bd.bodyId);

    bool damaged = false;
    for (auto &p : bd.originalPixels) {
      auto [tx, ty] = ProjectToWorld(p.first, p.second, pos, rot);
      if (grid.InBounds(tx, ty)) {
        Cell &c = grid.Get(tx, ty);
        // NOTE: Negative bodyID means the body
        // was damaged and require re-calculation
        // Stop the check since the whole body need refresh
        if (c.bodyID < 0) {
          damaged = true;
          break;
        }
      }
    }

    if (damaged) {
      // NOTE: Give released pixels a unique temporary ID so
      // body won't re-merge with pixels from a different breakage event.
      int32_t releaseID = nextReleaseID--;
      for (auto &p : bd.originalPixels) {
        auto [tx, ty] = ProjectToWorld(p.first, p.second, pos, rot);
        if (grid.InBounds(tx, ty)) {
          Cell &c = grid.Get(tx, ty);
          if (c.bodyID == selfID) {
            c.bodyID = releaseID;
          }
        }
      }
      // Destroy the old Box2D and remove the ejected
      // element out of the body
      b2DestroyBody(bd.bodyId);
      it = m_bodies.erase(it);
      continue;
    }
    it++;
  }
}

void RigidBodySystem::PostUpdate(Grid &grid, ElementContext &ctx,
                                 ParticleSystem &particles,
                                 SimulationConfig &config, float dt) {
  // 1. Clear old body pixels from grid using forward-mapping
  ClearBodiesFromGrid(grid, config);

  // 2. Rebuild static world meshes (only for dirty chunks)
  UpdateWorldMeshes(grid, ctx);

  // 3. Step physics
  b2World_Step(m_worldId, dt, 4);

  // 4. Fluid displacement & drag
  ProcessDisplacement(grid, ctx, particles);

  // 5. Write bodies back to grid
  SyncBackToGrid(grid, ctx);
}

void RigidBodySystem::ClearBodiesFromGrid(Grid &grid,
                                          SimulationConfig &config) {
  for (auto &bd : m_bodies) {

    b2Vec2 pos = b2Body_GetPosition(bd.bodyId);
    b2Rot rot = b2Body_GetRotation(bd.bodyId);
    const int32_t selfID = MakeBodyID(bd.bodyId);
    int maskW = bd.maxX - bd.minX + 1;

    for (auto &p : bd.originalPixels) {
      auto [wx, wy] = ProjectToWorld(p.first, p.second, pos, rot);
      if (grid.InBounds(wx, wy)) {
        Cell &c = grid.Get(wx, wy);
        if (c.bodyID == selfID) {
          // Keep what the world did to it (heat, cold) for the write-back
          int lx = (int)std::round(p.first) - bd.minX;
          int ly = (int)std::round(p.second) - bd.minY;
          bd.localTemperatures[ly * maskW + lx] = c.temperature;
          c = ElementFactory::Create(Element::AIR, config);
        }
      }
    }
  }
}

void RigidBodySystem::UpdateWorldMeshes(Grid &grid, ElementContext &ctx) {
  for (int cy = 0; cy < CHUNK_ROWS; ++cy) {
    for (int cx = 0; cx < CHUNK_COLS; ++cx) {
      int chunkIdx = cy * CHUNK_COLS + cx;
      auto &chunk = ctx.chunks.GetChunk(cx, cy);
      auto &mesh = m_chunkMeshes[chunkIdx];

      // Early-out: nothing changed since last build
      if (mesh.active && mesh.lastChangeFrame >= chunk.lastStaticChangeFrame) {
        continue;
      }

      // Destroy old body if any
      if (mesh.active) {
        b2DestroyBody(mesh.bodyId);
        mesh.active = false;
      }

      int x0 = cx * CHUNK_SIZE;
      int y0 = cy * CHUNK_SIZE;

      std::vector<bool> mask(CHUNK_SIZE * CHUNK_SIZE, false);
      bool hasSolid = false;

      float sumDensity = 0.0f;
      float sumRestitution = 0.0f;
      int countProps = 0;

      for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
          if (grid.InBounds(x0 + lx, y0 + ly)) {
            const Cell &c = grid.Get(x0 + lx, y0 + ly);
            const auto &cellProps =
                ctx.config.elements[static_cast<size_t>(c.element)];

            // Include in static chunk mesh if this element is explicitly
            // marked as static terrain, or if it's eligible for rigid-body
            // extraction but those bodies are not movable (therefore
            // effectively static).
            if (IsStaticCell(c, cellProps) ||
                (cellProps.rigidBodyCandidate && !cellProps.bodyMovable)) {
              mask[ly * CHUNK_SIZE + lx] = true;
              hasSolid = true;
              sumDensity += cellProps.density;
              sumRestitution += cellProps.restitution;
              ++countProps;
            }
          }
        }
      }

      // Only run triangulation is hasSolid flag was set
      if (hasSolid) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {(float)x0, (float)y0};
        mesh.bodyId = b2CreateBody(m_worldId, &bodyDef);
        mesh.lastChangeFrame = chunk.lastStaticChangeFrame;
        mesh.active = true;

        // Use averaged properties from included cells for shape creation.
        float avgDensity =
            (countProps > 0) ? (sumDensity / countProps) : 1000.0f;
        float avgRestitution =
            (countProps > 0) ? (sumRestitution / countProps) : 0.1f;
        AddTriangulatedShapes(mesh.bodyId, mask, CHUNK_SIZE, CHUNK_SIZE, 0, 0,
                              avgDensity, avgRestitution);
      }
    }
  }
}

void RigidBodySystem::ProcessDisplacement(Grid &grid, ElementContext &ctx,
                                          ParticleSystem &particles) {
  // Loose material inside a body's new footprint must go somewhere, or
  // SyncBackToGrid paints the body over it and it's lost. Fast bodies splash it
  // out as particles; slow ones nudge it just outside their footprint.
  constexpr float kSplashSpeed = 6.0f;
  constexpr int kNudgeReach = 3;

  auto rand01 = [&ctx]() { return (ctx.rng() % 10001) / 10000.0f; };

  for (auto &bd : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bd.bodyId);
    b2Rot rot = b2Body_GetRotation(bd.bodyId);
    b2Vec2 vel = b2Body_GetLinearVelocity(bd.bodyId);
    b2Vec2 center = b2Body_GetWorldCenter(bd.bodyId);
    float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    float dragForce = 0.0f;

    std::unordered_set<int64_t> footprint;
    footprint.reserve(bd.originalPixels.size());
    for (auto &p : bd.originalPixels) {
      auto [wx, wy] = ProjectToWorld(p.first, p.second, pos, rot);
      footprint.insert((int64_t)wx << 32 | (uint32_t)wy);
    }
    auto inFootprint = [&](int x, int y) {
      return footprint.contains((int64_t)x << 32 | (uint32_t)y);
    };

    for (auto &p : bd.originalPixels) {
      auto [wx, wy] = ProjectToWorld(p.first, p.second, pos, rot);
      if (!grid.InBounds(wx, wy))
        continue;

      Cell &c = grid.Get(wx, wy);
      if (c.bodyID != -1 || c.element == Element::AIR)
        continue;

      const auto &props = ctx.config.elements[static_cast<size_t>(c.element)];
      dragForce += props.density * 0.0005f;

      // Terrain is kept out by Box2D's static meshes; only loose material
      // (water, sand, gases) gets pushed around here
      if (!props.mobile)
        continue;

      Vector2 out{wx - center.x, wy - center.y};
      float outLen = std::sqrt(out.x * out.x + out.y * out.y);
      out = outLen > 0.001f ? Vector2{out.x / outLen, out.y / outLen}
                            : Vector2{0.0f, -1.0f};

      bool moved = false;
      if (speed < kSplashSpeed) {
        for (int k = 1; k <= kNudgeReach && !moved; ++k) {
          int nx = wx + (int)std::round(out.x * k);
          int ny = wy + (int)std::round(out.y * k);
          if (!grid.InBounds(nx, ny) || inFootprint(nx, ny))
            continue;
          Cell &dest = grid.Get(nx, ny);
          if (dest.element != Element::AIR)
            continue;
          dest = c;
          dest.vx += vel.x * 0.2f;
          dest.vy += vel.y * 0.2f;
          dest.lastUpdateFrame = ctx.frameIndex;
          ctx.chunks.WakeChunkAt(nx, ny, ctx.frameIndex);
          moved = true;
        }
      }

      if (!moved) {
        // Splash: carry the body's motion plus an outward kick, starting just
        // outside the footprint so it doesn't immediately land inside the body
        Vector2 from{wx + 0.5f, wy + 0.5f};
        for (int k = 1; k <= 8; ++k) {
          int nx = wx + (int)std::round(out.x * k);
          int ny = wy + (int)std::round(out.y * k);
          if (!inFootprint(nx, ny)) {
            from = {nx + 0.5f, ny + 0.5f};
            break;
          }
        }
        float kick = 3.0f + speed * 0.4f;
        Vector2 pVel{vel.x * 0.6f + out.x * kick + (rand01() - 0.5f) * 4.0f,
                     vel.y * 0.6f + out.y * kick - rand01() * 4.0f};
        if (Particle *sp = particles.Spawn(from, pVel, c.element)) {
          sp->temperature = c.temperature;
          moved = true;
        }
      }

      if (moved) {
        c = ElementFactory::Create(Element::AIR, ctx.config);
        ctx.chunks.WakeChunkAt(wx, wy, ctx.frameIndex);
      }
    }

    if (dragForce > 0.0f && speed > 0.1f) {
      b2Vec2 drag = {-vel.x / speed * dragForce, -vel.y / speed * dragForce};
      b2Body_ApplyForceToCenter(bd.bodyId, drag, true);
    }
  }
}

void RigidBodySystem::ApplyImpulse(int32_t cellBodyID, Vector2 impulse,
                                   Vector2 point) {
  for (auto &bd : m_bodies) {
    if (MakeBodyID(bd.bodyId) != cellBodyID)
      continue;
    b2Body_ApplyLinearImpulse(bd.bodyId, {impulse.x, impulse.y},
                              {point.x, point.y}, true);
    return;
  }
}

// After clearing, re-calibrate the bodies' pixels to their destined
// position right after box2d update was called and wake chunk up
void RigidBodySystem::SyncBackToGrid(Grid &grid, ElementContext &ctx) {
  for (auto &bd : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bd.bodyId);
    b2Rot rot = b2Body_GetRotation(bd.bodyId);
    const int32_t selfID = MakeBodyID(bd.bodyId);
    int maskW = bd.maxX - bd.minX + 1;

    for (size_t i = 0; i < bd.originalPixels.size(); ++i) {
      auto &p = bd.originalPixels[i];
      auto [wx, wy] = ProjectToWorld(p.first, p.second, pos, rot);
      if (!grid.InBounds(wx, wy))
        continue;

      Cell &c = grid.Get(wx, wy);

      if (c.bodyID == -1 && c.element != Element::AIR) {
        ctx.chunks.WakeChunkAt(wx, wy, ctx.frameIndex);
      }

      if (c.bodyID < 0 || c.bodyID == selfID) {
        int lx = (int)std::round(p.first) - bd.minX;
        int ly = (int)std::round(p.second) - bd.minY;
        int idx = ly * maskW + lx;
        Element bodyElement = bd.localElements[idx];
        if (c.element != bodyElement) {
          // Cell is taking on the body's material; don't inherit the old
          // cell's hardness (e.g. 0 from AIR), or anything can break it
          const auto &bodyProps =
              ctx.config.elements[static_cast<size_t>(bodyElement)];
          c.hardness = bodyProps.defaultHardness;
          c.density = bodyProps.density;
          c.mass = bodyProps.defaultMass;
        }
        c.element = bodyElement;
        c.bodyID = selfID;
        c.temperature = bd.localTemperatures[idx];
      }
    }
  }
}

namespace {

// Whether any cell of a blob sits on solid ground that isn't the blob
bool RestsOnSolid(const Grid &grid, const ElementContext &ctx,
                  const std::vector<std::pair<int, int>> &pixels,
                  Element element) {
  for (auto [x, y] : pixels) {
    if (!grid.InBounds(x, y + 1))
      return true; // the floor of the world
    const Cell &below = grid.Get(x, y + 1);
    if (below.element == element && below.bodyID == -1)
      continue; // the blob itself
    if (ctx.config.elements[static_cast<size_t>(below.element)].solid)
      return true;
  }
  return false;
}

} // namespace

void RigidBodySystem::ExtractDynamicBodies(Grid &grid, ElementContext &ctx,
                                           ParticleSystem &particles) {
  std::vector<bool> visited(GRID_W * GRID_H, false);

  // Loop through all pixels in the grid
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      int idx = y * GRID_W + x;

      // Avoid revisiting the same cell twice
      if (visited[idx])
        continue;

      Cell &cell = grid.Get(x, y);
      const auto &props =
          ctx.config.elements[static_cast<size_t>(cell.element)];

      // Only consider elements explicitly marked as rigid-body candidates
      // for extraction into Box2D bodies.
      if (!props.rigidBodyCandidate ||
          (cell.flags & (CELL_ANCHORED | CELL_HELD)))
        continue;

      // bodyID == -1 → never-been-a-body rigid pixel (freshly painted)
      // bodyID < -1  → released/orphaned pixel from a broken body
      const bool isFresh = (cell.bodyID == -1);
      const bool isOrphan = (cell.bodyID < -1);

      // Ignore the existing bodies
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

        // pixels indicate for all connected cells
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

      // Orphaned blobs (released from a broken body)
      if (isOrphan) {
        // If the connected blocks are way too small just turn them into
        //
        // particles
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
      }

      // Fresh pixels or large orphaned blobs: small blobs crumble to particles,
      // large blobs become a new rigid body. A small fresh blob resting on
      // something solid stays put and grows (a spell landing a few cells a
      // frame); crumbled, it would only land and crumble again.
      if (pixels.size() < 10 && isFresh &&
          RestsOnSolid(grid, ctx, pixels, startElement)) {
        continue;
      }
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

      // Calculate the center of mass
      float centerX = sumX / pixels.size();
      float centerY = sumY / pixels.size();

      b2BodyDef bodyDef = b2DefaultBodyDef();
      // Create dynamic or static body depending on element's property.
      // Determine wether it moves or stay still
      bodyDef.type = props.bodyMovable ? b2_dynamicBody : b2_staticBody;
      bodyDef.position = {centerX, centerY};
      // Disable auto-sleep for dynamic bodies so they respond to gravity
      // even when created alone. Static bodies don't use sleep.
      if (bodyDef.type == b2_dynamicBody)
        bodyDef.enableSleep = false;
      b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

      // Obtain te smallest rectangle containing every pixel
      int minX = pixels[0].first, maxX = minX;
      int minY = pixels[0].second, maxY = minY;
      for (auto &p : pixels) {
        minX = std::min(minX, p.first);
        maxX = std::max(maxX, p.first);
        minY = std::min(minY, p.second);
        maxY = std::max(maxY, p.second);
      }

      int width = maxX - minX + 1;
      int height = maxY - minY + 1;

      // Build the bit map wich convert world coords into local bounding box coord
      std::vector<bool> mask(width * height, false);
      for (auto &p : pixels)
        mask[(p.second - minY) * width + (p.first - minX)] = true;

      AddTriangulatedShapes(bodyId, mask, width, height, centerX - minX,
                            centerY - minY, props.density, props.restitution);
      b2Body_UpdateMassFromShapes(bodyId);

      // Update the body data from pre-calculated
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
      data.localTemperatures.assign(dw * dh, 0.0f);

      for (auto &p : pixels) {
        int lx = (int)std::round(p.first - centerX);
        int ly = (int)std::round(p.second - centerY);
        int kidx = (ly - data.minY) * dw + (lx - data.minX);
        data.pixelMask[kidx] = true;
        data.localElements[kidx] = startElement;
        data.localTemperatures[kidx] = grid.Get(p.first, p.second).temperature;
        data.elements.push_back(startElement);
        data.originalPixels.push_back({(float)lx, (float)ly});
        grid.Get(p.first, p.second).bodyID = MakeBodyID(bodyId);
      }
      m_bodies.push_back(data);
    }
  }
}

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

void RigidBodySystem::DrawDebug() {
  for (auto &bd : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bd.bodyId);
    b2Rot rot = b2Body_GetRotation(bd.bodyId);
    b2BodyType type = b2Body_GetType(bd.bodyId);
    float mass = b2Body_GetMass(bd.bodyId);
    bool isAwake = b2Body_IsAwake(bd.bodyId);

    Color debugColor = RED;
    if (type != b2_dynamicBody || mass <= 0.0f) {
      debugColor = BLUE;
    } else if (!isAwake) {
      debugColor = ORANGE;
    }

    int shapeCount = b2Body_GetShapeCount(bd.bodyId);
    if (shapeCount > 0) {
      std::vector<b2ShapeId> shapes(shapeCount);
      b2Body_GetShapes(bd.bodyId, shapes.data(), shapeCount);
      for (auto shapeId : shapes) {
        b2Polygon poly = b2Shape_GetPolygon(shapeId);
        for (int i = 0; i < poly.count; ++i) {
          int next = (i + 1) % poly.count;
          b2Vec2 p1 = b2TransformPoint({pos, rot}, poly.vertices[i]);
          b2Vec2 p2 = b2TransformPoint({pos, rot}, poly.vertices[next]);
          DrawLine((int)(p1.x * CELL_SIZE), (int)(p1.y * CELL_SIZE),
                   (int)(p2.x * CELL_SIZE), (int)(p2.y * CELL_SIZE),
                   debugColor);
        }
      }
    }
  }

  // Draw static terrain chunk meshes separately
  Color terrainColor = GREEN;
  for (int cy = 0; cy < CHUNK_ROWS; ++cy) {
    for (int cx = 0; cx < CHUNK_COLS; ++cx) {
      int chunkIdx = cy * CHUNK_COLS + cx;
      auto &mesh = m_chunkMeshes[chunkIdx];
      if (!mesh.active)
        continue;

      int shapeCount = b2Body_GetShapeCount(mesh.bodyId);
      if (shapeCount <= 0)
        continue;

      std::vector<b2ShapeId> shapes(shapeCount);
      b2Body_GetShapes(mesh.bodyId, shapes.data(), shapeCount);
      b2Vec2 pos = b2Body_GetPosition(mesh.bodyId);
      b2Rot rot = b2Body_GetRotation(mesh.bodyId);

      for (auto shapeId : shapes) {
        b2Polygon poly = b2Shape_GetPolygon(shapeId);
        for (int i = 0; i < poly.count; ++i) {
          int next = (i + 1) % poly.count;
          b2Vec2 p1 = b2TransformPoint({pos, rot}, poly.vertices[i]);
          b2Vec2 p2 = b2TransformPoint({pos, rot}, poly.vertices[next]);
          DrawLine((int)(p1.x * CELL_SIZE), (int)(p1.y * CELL_SIZE),
                   (int)(p2.x * CELL_SIZE), (int)(p2.y * CELL_SIZE),
                   terrainColor);
        }
      }
    }
  }
}

uint64_t RigidBodySystem::StateHash() const {
  uint64_t h = 0xcbf29ce484222325ull;
  auto add = [&h](float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 4; ++i) {
      h ^= (bits >> (i * 8)) & 0xFF;
      h *= 0x100000001b3ull;
    }
  };
  for (const BodyData &body : m_bodies) {
    if (!b2Body_IsValid(body.bodyId))
      continue;
    b2Vec2 p = b2Body_GetPosition(body.bodyId);
    b2Rot r = b2Body_GetRotation(body.bodyId);
    b2Vec2 v = b2Body_GetLinearVelocity(body.bodyId);
    add(p.x);
    add(p.y);
    add(r.c);
    add(r.s);
    add(v.x);
    add(v.y);
    add(b2Body_GetAngularVelocity(body.bodyId));
  }
  return h;
}
