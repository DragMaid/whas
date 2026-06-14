#include "whas/physics/rigid_body_system.h"
#include "whas/element/base/econtext.h"
#include <box2d/box2d.h>

RigidBodySystem::RigidBodySystem() {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.0f, 9.8f};
    m_worldId = b2CreateWorld(&worldDef);
}

RigidBodySystem::~RigidBodySystem() {
    b2DestroyWorld(m_worldId);
}

void RigidBodySystem::Update(Grid &grid, float dt) {
    b2World_Step(m_worldId, dt, 4);
    
    // Sync Box2D bodies back to grid cells (TODO)
}

void RigidBodySystem::ExtractBodies(Grid &grid, ElementContext &ctx) {
    // 1. Detection (Connected Components)
    // 2. Marching Squares (Contours)
    // 3. RDP (Simplification)
    // 4. Box2D Creation
}
