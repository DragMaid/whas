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
} // namespace

RigidBodySystem::RigidBodySystem() {
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = {0.0f, 9.8f};
  m_worldId = b2CreateWorld(&worldDef);
}

RigidBodySystem::~RigidBodySystem() { b2DestroyWorld(m_worldId); }
void RigidBodySystem::PreUpdate(Grid &grid, ElementContext &ctx) {
  // 0. Damage Check: If user erased pixels, mark body for destruction
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    int damageCount = 0;
    int totalPixels = 0;

    for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
      auto &p = bodyData.originalPixels[i];
      int tx = static_cast<int>(
          std::round(pos.x + p.first * rot.c - p.second * rot.s));
      int ty = static_cast<int>(
          std::round(pos.y + p.first * rot.s + p.second * rot.c));

      if (grid.InBounds(tx, ty)) {
        totalPixels++;
        Cell &c = grid.Get(tx, ty);
        // If user erased this pixel (turned to AIR and removed bodyID)
        if (c.element == Element::AIR && c.bodyID == -1) {
          damageCount++;
        }
      }
    }

    // If more than 10% of the body is erased, mark it to break
    if (totalPixels > 0 && (float)damageCount / totalPixels > 0.1f) {
      bodyData.shouldBreak = true;
    }
  }

  // 1. Inject RigidBody pixels into the grid as solid using hole-free logic
  for (auto it = m_bodies.begin(); it != m_bodies.end();) {
    auto &bodyData = *it;
    if (bodyData.shouldBreak) {
      b2DestroyBody(bodyData.bodyId);
      it = m_bodies.erase(it);
      continue;
    }

    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);

    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    float lxmin = bodyData.minX - 0.5f, lxmax = bodyData.maxX + 0.5f;
    float lymin = bodyData.minY - 0.5f, lymax = bodyData.maxY + 0.5f;

    b2Vec2 corners[4] = {
        {lxmin * rot.c - lymin * rot.s, lxmin * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymin * rot.s, lxmax * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymax * rot.s, lxmax * rot.s + lymax * rot.c},
        {lxmin * rot.c - lymax * rot.s, lxmin * rot.s + lymax * rot.c}};

    int wMinX = GRID_W, wMaxX = 0, wMinY = GRID_H, wMaxY = 0;
    for (auto &c : corners) {
      wMinX = std::min(wMinX, (int)std::floor(pos.x + c.x));
      wMaxX = std::max(wMaxX, (int)std::ceil(pos.x + c.x));
      wMinY = std::min(wMinY, (int)std::floor(pos.y + c.y));
      wMaxY = std::max(wMaxY, (int)std::ceil(pos.y + c.y));
    }

    wMinX = std::max(0, wMinX);
    wMaxX = std::min(GRID_W - 1, wMaxX);
    wMinY = std::max(0, wMinY);
    wMaxY = std::min(GRID_H - 1, wMaxY);

    int maskW = bodyData.maxX - bodyData.minX + 1;
    int maskH = bodyData.maxY - bodyData.minY + 1;
    const int maskSize = maskW * maskH;

    for (int ty = wMinY; ty <= wMaxY; ++ty) {
      for (int tx = wMinX; tx <= wMaxX; ++tx) {
        float dx = (float)tx - pos.x;
        float dy = (float)ty - pos.y;
        float lx = dx * rot.c + dy * rot.s;
        float ly = -dx * rot.s + dy * rot.c;

        int ilx = (int)std::floor(lx + 0.5f);
        int ily = (int)std::floor(ly + 0.5f);

        if (ilx >= bodyData.minX && ilx <= bodyData.maxX &&
            ily >= bodyData.minY && ily <= bodyData.maxY) {
          int idx = (ily - bodyData.minY) * maskW + (ilx - bodyData.minX);
          if (idx >= 0 && idx < maskSize && bodyData.pixelMask[idx]) {
            Cell &c = grid.Get(tx, ty);
            c.element = bodyData.localElements[idx];
            c.bodyID = selfID;
          }
        }
      }
    }
    ++it;
  }
}

void RigidBodySystem::PostUpdate(Grid &grid, ElementContext &ctx,
                                 ParticleSystem &particles, float dt) {
  // 1. Remove RigidBody pixels from grid using hole-free logic
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    float lxmin = bodyData.minX - 0.5f, lxmax = bodyData.maxX + 0.5f;
    float lymin = bodyData.minY - 0.5f, lymax = bodyData.maxY + 0.5f;

    b2Vec2 corners[4] = {
        {lxmin * rot.c - lymin * rot.s, lxmin * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymin * rot.s, lxmax * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymax * rot.s, lxmax * rot.s + lymax * rot.c},
        {lxmin * rot.c - lymax * rot.s, lxmin * rot.s + lymax * rot.c}};

    int wMinX = GRID_W, wMaxX = 0, wMinY = GRID_H, wMaxY = 0;
    for (auto &c : corners) {
      wMinX = std::min(wMinX, (int)std::floor(pos.x + c.x));
      wMaxX = std::max(wMaxX, (int)std::ceil(pos.x + c.x));
      wMinY = std::min(wMinY, (int)std::floor(pos.y + c.y));
      wMaxY = std::max(wMaxY, (int)std::ceil(pos.y + c.y));
    }

    wMinX = std::max(0, wMinX);
    wMaxX = std::min(GRID_W - 1, wMaxX);
    wMinY = std::max(0, wMinY);
    wMaxY = std::min(GRID_H - 1, wMaxY);

    int maskW = bodyData.maxX - bodyData.minX + 1;
    int maskH = bodyData.maxY - bodyData.minY + 1;
    const int maskSize = maskW * maskH;

    for (int ty = wMinY; ty <= wMaxY; ++ty) {
      for (int tx = wMinX; tx <= wMaxX; ++tx) {
        float dx = (float)tx - pos.x;
        float dy = (float)ty - pos.y;
        float lx = dx * rot.c + dy * rot.s;
        float ly = -dx * rot.s + dy * rot.c;

        int ilx = (int)std::floor(lx + 0.5f);
        int ily = (int)std::floor(ly + 0.5f);

        if (ilx >= bodyData.minX && ilx <= bodyData.maxX &&
            ily >= bodyData.minY && ily <= bodyData.maxY) {
          int idx = (ily - bodyData.minY) * maskW + (ilx - bodyData.minX);
          if (idx >= 0 && idx < maskSize && bodyData.pixelMask[idx]) {
            Cell &c = grid.Get(tx, ty);
            if (c.bodyID == selfID) {
              c = ElementFactory::Create(Element::AIR, ctx.config);
            }
          }
        }
      }
    }
  }

  // 2. Update World Meshes (Chunk-based static collision)
  UpdateWorldMeshes(grid, ctx);

  // 3. Step Physics
  b2World_Step(m_worldId, dt, 4);

  // 4. Process Displacement & Drag
  ProcessDisplacement(grid, ctx, particles);

  // 5. Sync back to grid
  SyncBackToGrid(grid, ctx);
}

void RigidBodySystem::UpdateWorldMeshes(Grid &grid, ElementContext &ctx) {
  // For each chunk near a rigid body, regenerate mesh if changed
  // For simplicity in this refactor, we'll check all active chunks
  for (int cy = 0; cy < CHUNK_ROWS; ++cy) {
    for (int cx = 0; cx < CHUNK_COLS; ++cx) {
      int chunkIdx = cy * CHUNK_COLS + cx;
      auto &chunk = ctx.chunks.GetChunk(cx, cy);

      bool needsRegen =
          (m_chunkMeshes.find(chunkIdx) == m_chunkMeshes.end()) ||
          (m_chunkMeshes[chunkIdx].lastChangeFrame < chunk.lastChangeFrame);

      if (needsRegen) {
        if (m_chunkMeshes.count(chunkIdx)) {
          b2DestroyBody(m_chunkMeshes[chunkIdx].bodyId);
        }

        // Generate mask for EARTH/Solid pixels in this chunk
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
          m_chunkMeshes[chunkIdx] = {bodyId, chunk.lastChangeFrame};
        } else {
          m_chunkMeshes.erase(chunkIdx);
        }
      }
    }
  }
}

void RigidBodySystem::ProcessDisplacement(Grid &grid, ElementContext &ctx,
                                          ParticleSystem &particles) {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    b2Vec2 vel = b2Body_GetLinearVelocity(bodyData.bodyId);
    float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);

    float dragForce = 0.0f;

    // Use hole-free logic to find cells being displaced by the NEW position
    float lxmin = bodyData.minX - 0.5f, lxmax = bodyData.maxX + 0.5f;
    float lymin = bodyData.minY - 0.5f, lymax = bodyData.maxY + 0.5f;

    b2Vec2 corners[4] = {
        {lxmin * rot.c - lymin * rot.s, lxmin * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymin * rot.s, lxmax * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymax * rot.s, lxmax * rot.s + lymax * rot.c},
        {lxmin * rot.c - lymax * rot.s, lxmin * rot.s + lymax * rot.c}};

    int wMinX = GRID_W, wMaxX = 0, wMinY = GRID_H, wMaxY = 0;
    for (auto &c : corners) {
      wMinX = std::min(wMinX, (int)std::floor(pos.x + c.x));
      wMaxX = std::max(wMaxX, (int)std::ceil(pos.x + c.x));
      wMinY = std::min(wMinY, (int)std::floor(pos.y + c.y));
      wMaxY = std::max(wMaxY, (int)std::ceil(pos.y + c.y));
    }

    wMinX = std::max(0, wMinX);
    wMaxX = std::min(GRID_W - 1, wMaxX);
    wMinY = std::max(0, wMinY);
    wMaxY = std::min(GRID_H - 1, wMaxY);

    int maskW = bodyData.maxX - bodyData.minX + 1;
    int maskH = bodyData.maxY - bodyData.minY + 1;
    const int maskSize = maskW * maskH;

    for (int ty = wMinY; ty <= wMaxY; ++ty) {
      for (int tx = wMinX; tx <= wMaxX; ++tx) {
        float dx = (float)tx - pos.x;
        float dy = (float)ty - pos.y;
        float lx = dx * rot.c + dy * rot.s;
        float ly = -dx * rot.s + dy * rot.c;

        int ilx = (int)std::floor(lx + 0.5f);
        int ily = (int)std::floor(ly + 0.5f);

        if (ilx >= bodyData.minX && ilx <= bodyData.maxX &&
            ily >= bodyData.minY && ily <= bodyData.maxY) {
          int idx = (ily - bodyData.minY) * maskW + (ilx - bodyData.minX);
          if (idx >= 0 && idx < maskSize && bodyData.pixelMask[idx]) {
            Cell &c = grid.Get(tx, ty);
            const auto &props =
                ctx.config.elements[static_cast<size_t>(c.element)];

            // TODO: this doesn't make any sense, it should handle for all
            // different types If it's sand or liquid and not a rigid body
            if (c.bodyID == -1 &&
                (c.element == Element::WATER || c.element == Element::SAND)) {
              // Splash effect: Turn into particle
              if (speed > 5.0f) {
                Vector2 pVel = {vel.x * 0.5f +
                                    (float)((ctx.rng() % 100) - 50) * 0.1f,
                                vel.y * 0.5f - (float)(ctx.rng() % 50) * 0.1f};
                particles.Spawn({(float)tx, (float)ty}, pVel, c.element);
              }

              c = ElementFactory::Create(Element::AIR, ctx.config);
              dragForce += props.density * 0.001f;
            }
          }
        }
      }
    }

    if (dragForce > 0.0f && speed > 0.1f) {
      b2Vec2 drag = {-vel.x / speed * dragForce, -vel.y / speed * dragForce};
      b2Body_ApplyForceToCenter(bodyData.bodyId, drag, true);
    }
  }
}

void RigidBodySystem::SyncBackToGrid(Grid &grid, ElementContext &ctx) {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    // Use the hole-free rendering approach
    float lxmin = bodyData.minX - 0.5f, lxmax = bodyData.maxX + 0.5f;
    float lymin = bodyData.minY - 0.5f, lymax = bodyData.maxY + 0.5f;

    b2Vec2 corners[4] = {
        {lxmin * rot.c - lymin * rot.s, lxmin * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymin * rot.s, lxmax * rot.s + lymin * rot.c},
        {lxmax * rot.c - lymax * rot.s, lxmax * rot.s + lymax * rot.c},
        {lxmin * rot.c - lymax * rot.s, lxmin * rot.s + lymax * rot.c}};

    int wMinX = GRID_W, wMaxX = 0, wMinY = GRID_H, wMaxY = 0;
    for (auto &c : corners) {
      wMinX = std::min(wMinX, (int)std::floor(pos.x + c.x));
      wMaxX = std::max(wMaxX, (int)std::ceil(pos.x + c.x));
      wMinY = std::min(wMinY, (int)std::floor(pos.y + c.y));
      wMaxY = std::max(wMaxY, (int)std::ceil(pos.y + c.y));
    }

    wMinX = std::max(0, wMinX);
    wMaxX = std::min(GRID_W - 1, wMaxX);
    wMinY = std::max(0, wMinY);
    wMaxY = std::min(GRID_H - 1, wMaxY);

    int maskW = bodyData.maxX - bodyData.minX + 1;
    int maskH = bodyData.maxY - bodyData.minY + 1;
    const int maskSize = maskW * maskH;

    for (int ty = wMinY; ty <= wMaxY; ++ty) {
      for (int tx = wMinX; tx <= wMaxX; ++tx) {
        float dx = (float)tx - pos.x;
        float dy = (float)ty - pos.y;
        float lx = dx * rot.c + dy * rot.s;
        float ly = -dx * rot.s + dy * rot.c;

        int ilx = (int)std::floor(lx + 0.5f);
        int ily = (int)std::floor(ly + 0.5f);

        if (ilx >= bodyData.minX && ilx <= bodyData.maxX &&
            ily >= bodyData.minY && ily <= bodyData.maxY) {
          int idx = (ily - bodyData.minY) * maskW + (ilx - bodyData.minX);
          if (idx >= 0 && idx < maskSize && bodyData.pixelMask[idx]) {
            Cell &c = grid.Get(tx, ty);

            // Splash effect: If we are moving fast and hit water
            if (c.element == Element::WATER && c.bodyID == -1) {
              b2Vec2 vel = b2Body_GetLinearVelocity(bodyData.bodyId);
              float speedSq = vel.x * vel.x + vel.y * vel.y;
              if (speedSq > 25.0f) { // Speed threshold for splash
                c.vy += vel.y * 0.1f;
                c.vx += vel.x * 0.1f;
                // Wake up the water
                ctx.chunks.WakeChunkAt(tx, ty, ctx.frameIndex);
              }
            }

            if (c.bodyID == -1 || c.bodyID == selfID) {
              c.element = bodyData.localElements[idx];
              c.bodyID = selfID;
            }
          }
        }
      }
    }
  }
}

void RigidBodySystem::ExtractDynamicBodies(Grid &grid, ElementContext &ctx) {
  std::vector<bool> visited(GRID_W * GRID_H, false);
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      int idx = y * GRID_W + x;
      if (visited[idx])
        continue;

      Cell &cell = grid.Get(x, y);
      const auto &props =
          ctx.config.elements[static_cast<size_t>(cell.element)];

      if (props.rigidBody && cell.bodyID == -1) {
        std::vector<std::pair<int, int>> pixels;
        std::queue<std::pair<int, int>> q;
        q.push({x, y});
        visited[idx] = true;
        float sumX = 0, sumY = 0;
        Element startElement = cell.element;

        while (!q.empty()) {
          auto [cx, cy] = q.front();
          q.pop();
          pixels.push_back({cx, cy});
          sumX += cx;
          sumY += cy;
          int dx[] = {0, 0, 1, -1}, dy[] = {1, -1, 0, 0};
          for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (grid.InBounds(nx, ny) && !visited[ny * GRID_W + nx] &&
                grid.Get(nx, ny).element == startElement) {
              visited[ny * GRID_W + nx] = true;
              q.push({nx, ny});
            }
          }
        }

        float centerX = sumX / pixels.size(), centerY = sumY / pixels.size();
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {centerX, centerY};
        b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

        int minX = pixels[0].first, maxX = minX, minY = pixels[0].second,
            maxY = minY;
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
        data.pixelMask.assign(
            (data.maxX - data.minX + 1) * (data.maxY - data.minY + 1), false);
        data.localElements.assign(data.pixelMask.size(), Element::AIR);

        for (auto &p : pixels) {
          int lx = (int)std::round(p.first - centerX),
              ly = (int)std::round(p.second - centerY);
          int kidx =
              (ly - data.minY) * (data.maxX - data.minX + 1) + (lx - data.minX);
          data.pixelMask[kidx] = true;
          data.localElements[kidx] = startElement;
          data.elements.push_back(startElement);
          data.originalPixels.push_back({(float)lx, (float)ly});
        }
        m_bodies.push_back(data);
      }
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
  // shapeDef.friction = 0.4f;
  // shapeDef.restitution = restitution;

  for (auto &loop : loops) {
    auto simplified = GeometryUtils::DouglasPeucker(loop, 0.5f);
    auto triangles = GeometryUtils::Triangulate(simplified);
    for (auto &tri : triangles) {
      b2Vec2 points[3];
      for (int i = 0; i < 3; ++i) {
        points[i] = {tri[i].x - offX, tri[i].y - offY};
      }

      b2Hull hull = b2ComputeHull(points, 3);
      if (hull.count == 3) {
        b2Polygon poly = b2MakePolygon(&hull, 0.0f);
        b2CreatePolygonShape(bodyId, &shapeDef, &poly);
      }
    }
  }
}

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
