#pragma once
#include "whas/core/element.h"
#include <vector>
#include <raylib.h>

struct Particle {
    Vector2 pos;
    Vector2 vel;
    Element element;
    bool active = false;
    bool spellActive = false;
    bool isProjectile = false;
    float remainingDistance = 0.0f;
};

class ParticleSystem {
public:
    ParticleSystem(int maxParticles = 2000);
    
    void Spawn(Vector2 pos, Vector2 vel, Element element,
               float remainingDistance = 0.0f, bool spellActive = false,
               bool isProjectile = false);
    void Update(struct Grid& grid, struct ElementContext& ctx, float dt);
    void Draw();

private:
    std::vector<Particle> m_particles;
    int m_maxParticles;
};
