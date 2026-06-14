#include "whas/physics/rigid_body_system.h"
#include "raylib.h"
#include "whas/constants.h"
#include "whas/element/base/econtext.h"
#include "whas/element/base/factory.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace {
// Combines index1 and generation into a single 32-bit ID so that reused
// Box2D body slots (same index1, new generation) don't alias old grid
// markers left behind after b2DestroyBody.
inline int32_t MakeBodyID(b2BodyId id) {
  return (static_cast<int32_t>(id.index1) << 16) |
         (static_cast<int32_t>(id.generation) & 0xFFFF);
}
} // namespace

RigidBodySystem::RigidBodySystem() {
  b2WorldDef worldDef = b2DefaultWorldDef();
  worldDef.gravity = {0.0f, 9.8f};
  m_worldId = b2CreateWorld(&worldDef);

  // Create a static floor at the bottom
  b2BodyDef groundBodyDef = b2DefaultBodyDef();
  groundBodyDef.position = {(float)GRID_W / 2.0f, (float)GRID_H};
  b2BodyId groundId = b2CreateBody(m_worldId, &groundBodyDef);

  b2Polygon groundBox = b2MakeBox((float)GRID_W / 2.0f, 1.0f);
  b2ShapeDef groundShapeDef = b2DefaultShapeDef();
  b2CreatePolygonShape(groundId, &groundShapeDef, &groundBox);
}

RigidBodySystem::~RigidBodySystem() { b2DestroyWorld(m_worldId); }

void RigidBodySystem::Update(Grid &grid, float dt) {
  SimulationConfig defaultConfig;

  // 1. Clear old pixels and handle "damage"
  for (auto it = m_bodies.begin(); it != m_bodies.end();) {
    auto &bodyData = *it;
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);

    bool bodyBroken = false;
    const int32_t selfID = MakeBodyID(bodyData.bodyId);

    // Check if any pixels were removed/changed
    for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
      auto &p = bodyData.originalPixels[i];
      int tx = static_cast<int>(
          std::round(pos.x + p.first * rot.c - p.second * rot.s));
      int ty = static_cast<int>(
          std::round(pos.y + p.first * rot.s + p.second * rot.c));

      if (grid.InBounds(tx, ty)) {
        Cell &c = grid.Get(tx, ty);
        if (c.bodyID != selfID || c.element != bodyData.elements[i]) {
          bodyBroken = true;
          break;
        }
      } else {
        bodyBroken = true;
        break;
      }
    }

    if (bodyBroken) {
      for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
        auto &p = bodyData.originalPixels[i];
        int tx = static_cast<int>(
            std::round(pos.x + p.first * rot.c - p.second * rot.s));
        int ty = static_cast<int>(
            std::round(pos.y + p.first * rot.s + p.second * rot.c));
        if (grid.InBounds(tx, ty)) {
          Cell &c = grid.Get(tx, ty);
          if (c.bodyID == selfID)
            c.bodyID = -1;
        }
      }
      b2DestroyBody(bodyData.bodyId);
      it = m_bodies.erase(it);
      continue;
    }

    // Clear for movement
    for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
      auto &p = bodyData.originalPixels[i];
      int tx = static_cast<int>(
          std::round(pos.x + p.first * rot.c - p.second * rot.s));
      int ty = static_cast<int>(
          std::round(pos.y + p.first * rot.s + p.second * rot.c));
      if (grid.InBounds(tx, ty)) {
        grid.Get(tx, ty) = ElementFactory::Create(Element::AIR, defaultConfig);
      }
    }
    ++it;
  }

  // 2. Step physics
  b2World_Step(m_worldId, dt, 4);

  // 3. Sync bodies back to grid (Hole-free rendering)
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
        float dx = tx - pos.x;
        float dy = ty - pos.y;
        float lx = dx * rot.c + dy * rot.s;
        float ly = -dx * rot.s + dy * rot.c;

        int ilx = (int)std::round(lx);
        int ily = (int)std::round(ly);

        if (ilx >= bodyData.minX && ilx <= bodyData.maxX &&
            ily >= bodyData.minY && ily <= bodyData.maxY) {
          int idx = (ily - bodyData.minY) * maskW + (ilx - bodyData.minX);

          // Defensive bounds check: guards against any future
          // mismatch between min/max bounds and mask dimensions.
          if (idx < 0 || idx >= maskSize)
            continue;

          if (bodyData.pixelMask[idx]) {
            Cell &c = grid.Get(tx, ty);
            if (c.element == Element::AIR || c.bodyID == selfID) {
              c.element = bodyData.localElements[idx];
              c.bodyID = selfID;
            }
          }
        }
      }
    }
  }
}

void RigidBodySystem::DrawDebug() {
  for (auto &bodyData : m_bodies) {
    b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
    b2Rot rot = b2Body_GetRotation(bodyData.bodyId);

    int width = bodyData.maxX - bodyData.minX + 1;
    int height = bodyData.maxY - bodyData.minY + 1;

    // Draw pixel mask
    for (int ly = 0; ly < height; ++ly) {
      for (int lx = 0; lx < width; ++lx) {
        if (bodyData.pixelMask[ly * width + lx]) {
          float localX = bodyData.minX + lx;
          float localY = bodyData.minY + ly;
          float tx = pos.x + localX * rot.c - localY * rot.s;
          float ty = pos.y + localX * rot.s + localY * rot.c;
          DrawPixel((int)(tx * CELL_SIZE), (int)(ty * CELL_SIZE),
                    Fade(GREEN, 0.4f));
        }
      }
    }

    // Draw hitboxes
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

void RigidBodySystem::ExtractBodies(Grid &grid, ElementContext &ctx) {
  for (auto bodyId : m_staticBodies)
    b2DestroyBody(bodyId);
  m_staticBodies.clear();

  std::vector<bool> visited(GRID_W * GRID_H, false);
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      Cell &c = grid.Get(x, y);
      if (c.element == Element::EARTH)
        c.bodyID = -1;
    }
  }

  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      int idx = y * GRID_W + x;
      if (visited[idx])
        continue;

      Cell &cell = grid.Get(x, y);
      const auto &props =
          ctx.config.elements[static_cast<size_t>(cell.element)];
      bool isDynamic = props.rigidBody;
      bool isStatic = (cell.element == Element::EARTH);

      if ((isDynamic || isStatic) && cell.bodyID == -1) {
        std::vector<std::pair<int, int>> componentPixels;
        std::vector<Element> componentElements;
        std::queue<std::pair<int, int>> q;

        q.push({x, y});
        visited[idx] = true;
        float sumX = 0, sumY = 0;
        Element startElement = cell.element;

        while (!q.empty()) {
          auto [cx, cy] = q.front();
          q.pop();
          componentPixels.push_back({cx, cy});
          componentElements.push_back(grid.Get(cx, cy).element);
          sumX += cx;
          sumY += cy;

          int dx[] = {0, 0, 1, -1}, dy[] = {1, -1, 0, 0};
          for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (grid.InBounds(nx, ny)) {
              int nidx = ny * GRID_W + nx;
              if (!visited[nidx]) {
                Cell &nb = grid.Get(nx, ny);
                if (nb.element == startElement && nb.bodyID == -1) {
                  visited[nidx] = true;
                  q.push({nx, ny});
                }
              }
            }
          }
        }

        if (componentPixels.empty())
          continue;

        float centerX = sumX / componentPixels.size();
        float centerY = sumY / componentPixels.size();

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = isDynamic ? b2_dynamicBody : b2_staticBody;
        bodyDef.position = {centerX, centerY};
        b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

        AddGreedyShapes(bodyId, componentPixels, centerX, centerY,
                        props.density);

        if (isDynamic) {
          BodyData data;
          data.bodyId = bodyId;
          int minX = componentPixels[0].first, maxX = minX;
          int minY = componentPixels[0].second, maxY = minY;
          for (auto &p : componentPixels) {
            minX = std::min(minX, p.first);
            maxX = std::max(maxX, p.first);
            minY = std::min(minY, p.second);
            maxY = std::max(maxY, p.second);
          }

          data.minX = (int)std::floor(minX - centerX);
          data.maxX = (int)std::ceil(maxX - centerX);
          data.minY = (int)std::floor(minY - centerY);
          data.maxY = (int)std::ceil(maxY - centerY);

          int width = data.maxX - data.minX + 1;
          int height = data.maxY - data.minY + 1;
          data.pixelMask.assign(width * height, false);
          data.localElements.assign(width * height, Element::AIR);

          const int32_t newBodyID = MakeBodyID(bodyId);

          for (size_t i = 0; i < componentPixels.size(); ++i) {
            auto &p = componentPixels[i];
            int lx = (int)std::round(p.first - centerX);
            int ly = (int)std::round(p.second - centerY);

            // round() can land one cell outside the floor/ceil
            // derived [minX, maxX] x [minY, maxY] range (e.g. when
            // (p - center) is exactly N.5 and rounds away from the
            // bound). Clamp before indexing to avoid an
            // out-of-bounds heap write into pixelMask/localElements.
            lx = std::clamp(lx, data.minX, data.maxX);
            ly = std::clamp(ly, data.minY, data.maxY);

            int kidx = (ly - data.minY) * width + (lx - data.minX);
            if (kidx < 0 || kidx >= width * height)
              continue;

            data.pixelMask[kidx] = true;
            data.localElements[kidx] = componentElements[i];
            data.originalPixels.push_back({(float)lx, (float)ly});
            data.elements.push_back(componentElements[i]);
          }
          m_bodies.push_back(data);

          for (auto &p : componentPixels)
            grid.Get(p.first, p.second).bodyID = newBodyID;
        } else {
          m_staticBodies.push_back(bodyId);
          const int32_t staticBodyID = MakeBodyID(bodyId);
          for (auto &p : componentPixels)
            grid.Get(p.first, p.second).bodyID = staticBodyID;
        }
      }
    }
  }
}

void RigidBodySystem::AddGreedyShapes(
    b2BodyId bodyId, const std::vector<std::pair<int, int>> &pixels,
    float centerX, float centerY, float density) {
  if (pixels.empty())
    return;

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
  std::vector<bool> localMask(width * height, false);
  for (auto &p : pixels) {
    localMask[(p.second - minY) * width + (p.first - minX)] = true;
  }

  std::vector<bool> used = localMask;
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = density / 1000.0f;
  // TODO: add the friction later
  // shapeDef.friction = 0.3f;

  for (int ly = 0; ly < height; ++ly) {
    for (int lx = 0; lx < width; ++lx) {
      if (used[ly * width + lx]) {
        int rw = 0;
        while (lx + rw < width && used[ly * width + lx + rw])
          rw++;

        int rh = 1;
        while (ly + rh < height) {
          bool rowAllGood = true;
          for (int i = 0; i < rw; ++i) {
            if (!used[(ly + rh) * width + lx + i]) {
              rowAllGood = false;
              break;
            }
          }
          if (!rowAllGood)
            break;
          rh++;
        }

        for (int iy = ly; iy < ly + rh; ++iy) {
          for (int ix = lx; ix < lx + rw; ++ix) {
            used[iy * width + ix] = false;
          }
        }

        float hx = rw * 0.5f;
        float hy = rh * 0.5f;
        float offX = (minX + lx + hx) - centerX;
        float offY = (minY + ly + hy) - centerY;

        b2Polygon box = b2MakeBox(hx, hy);
        for (int i = 0; i < box.count; ++i) {
          box.vertices[i].x += offX;
          box.vertices[i].y += offY;
        }
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
      }
    }
  }
}
