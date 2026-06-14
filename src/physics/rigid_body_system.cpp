#include "whas/physics/rigid_body_system.h"
#include "whas/element/base/econtext.h"
#include "whas/constants.h"
#include "whas/element/base/factory.h"
#include <queue>
#include <cmath>
#include <algorithm>

RigidBodySystem::RigidBodySystem() {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, 9.8f};
    m_worldId = b2CreateWorld(&worldDef);
}

RigidBodySystem::~RigidBodySystem() {
    b2DestroyWorld(m_worldId);
}

void RigidBodySystem::Update(Grid &grid, float dt) {
    SimulationConfig defaultConfig;

    // 1. Clear old pixels and handle "damage" (pixels that changed element)
    for (auto it = m_bodies.begin(); it != m_bodies.end(); ) {
        auto &bodyData = *it;
        b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
        float angle = b2Rot_GetAngle(b2Body_GetRotation(bodyData.bodyId));
        
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        bool bodyModified = false;
        for (size_t i = 0; i < bodyData.originalPixels.size(); ) {
            auto &p = bodyData.originalPixels[i];
            int tx = static_cast<int>(std::floor(pos.x + p.first * cosA - p.second * sinA));
            int ty = static_cast<int>(std::floor(pos.y + p.first * sinA + p.second * cosA));
            
            bool pixelChanged = false;
            if (grid.InBounds(tx, ty)) {
                Cell& c = grid.Get(tx, ty);
                if (c.bodyID == (int32_t)bodyData.bodyId.index1) {
                    if (c.element != bodyData.elements[i]) {
                        // Pixel changed externally (e.g. melted or erased)
                        pixelChanged = true;
                    } else {
                        // Still part of the body, clear it for movement
                        c = ElementFactory::Create(Element::AIR, defaultConfig);
                    }
                } else {
                    // Pixel was overwritten by something else
                    pixelChanged = true;
                }
            } else {
                // Out of bounds, consider it gone? 
                // For now, keep it in the body data but don't draw it.
                // Actually, if it's out of bounds, we can't clear it from grid anyway.
            }

            if (pixelChanged) {
                bodyData.originalPixels.erase(bodyData.originalPixels.begin() + i);
                bodyData.elements.erase(bodyData.elements.begin() + i);
                bodyModified = true;
                // Don't increment i
            } else {
                ++i;
            }
        }

        if (bodyData.originalPixels.empty()) {
            b2DestroyBody(bodyData.bodyId);
            it = m_bodies.erase(it);
        } else {
            if (bodyModified) {
                // If body was modified, we should ideally recreate shapes.
                // For now, we'll just keep the old shape (bounding box).
                // A better way would be to re-extract if many pixels are gone.
            }
            ++it;
        }
    }

    // 2. Step physics
    b2World_Step(m_worldId, dt, 4);

    // 3. Sync bodies back to grid
    for (auto &bodyData : m_bodies) {
        b2Vec2 pos = b2Body_GetPosition(bodyData.bodyId);
        float angle = b2Rot_GetAngle(b2Body_GetRotation(bodyData.bodyId));
        
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        for (size_t i = 0; i < bodyData.originalPixels.size(); ++i) {
            auto &p = bodyData.originalPixels[i];
            int tx = static_cast<int>(std::floor(pos.x + p.first * cosA - p.second * sinA));
            int ty = static_cast<int>(std::floor(pos.y + p.first * sinA + p.second * cosA));
            
            if (grid.InBounds(tx, ty)) {
                Cell& c = grid.Get(tx, ty);
                // Draw pixel back
                c.element = bodyData.elements[i];
                c.bodyID = (int32_t)bodyData.bodyId.index1;
            }
        }
    }
}

void RigidBodySystem::ExtractBodies(Grid &grid, ElementContext &ctx) {
    std::vector<bool> visited(GRID_W * GRID_H, false);
    
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            int idx = y * GRID_W + x;
            if (visited[idx]) continue;
            
            Cell &cell = grid.Get(x, y);
            const auto &props = ctx.config.elements[static_cast<size_t>(cell.element)];
            
            if (props.rigidBody && cell.bodyID == -1) {
                // Found a new component
                std::vector<std::pair<int, int>> componentPixels;
                std::vector<Element> componentElements;
                std::queue<std::pair<int, int>> q;
                
                q.push({x, y});
                visited[idx] = true;
                
                float sumX = 0, sumY = 0;
                
                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();
                    
                    componentPixels.push_back({cx, cy});
                    componentElements.push_back(grid.Get(cx, cy).element);
                    sumX += cx;
                    sumY += cy;
                    
                    int dx[] = {0, 0, 1, -1};
                    int dy[] = {1, -1, 0, 0};
                    
                    for (int i = 0; i < 4; ++i) {
                        int nx = cx + dx[i], ny = cy + dy[i];
                        if (grid.InBounds(nx, ny)) {
                            int nidx = ny * GRID_W + nx;
                            if (!visited[nidx]) {
                                Cell &nb = grid.Get(nx, ny);
                                if (nb.element == cell.element && nb.bodyID == -1) {
                                    visited[nidx] = true;
                                    q.push({nx, ny});
                                }
                            }
                        }
                    }
                }
                
                if (componentPixels.empty()) continue;
                
                // Create body
                float centerX = sumX / componentPixels.size();
                float centerY = sumY / componentPixels.size();
                
                b2BodyDef bodyDef = b2DefaultBodyDef();
                bodyDef.type = b2_dynamicBody;
                bodyDef.position = {centerX, centerY};
                b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);
                
                // For simplicity, create a box shape for each pixel (NOT efficient but works for now)
                // Actually, let's just create one box for the bounding box for now to demonstrate.
                // Or better: greedy meshing would be too long. Let's do a single box for the whole thing.
                int minX = componentPixels[0].first, maxX = minX;
                int minY = componentPixels[0].second, maxY = minY;
                for (auto &p : componentPixels) {
                    minX = std::min(minX, p.first);
                    maxX = std::max(maxX, p.first);
                    minY = std::min(minY, p.second);
                    maxY = std::max(maxY, p.second);
                }
                
                float hx = (maxX - minX + 1) * 0.5f;
                float hy = (maxY - minY + 1) * 0.5f;
                
                b2Polygon box = b2MakeBox(hx, hy);
                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.density = props.density / 1000.0f; // Scale density
                b2CreatePolygonShape(bodyId, &shapeDef, &box);
                
                BodyData data;
                data.bodyId = bodyId;
                for (auto &p : componentPixels) {
                    data.originalPixels.push_back({p.first - centerX, p.second - centerY});
                }
                data.elements = componentElements;
                m_bodies.push_back(data);
                
                // Mark in grid
                for (auto &p : componentPixels) {
                    grid.Get(p.first, p.second).bodyID = (int32_t)bodyId.index1;
                }
            }
        }
    }
}
