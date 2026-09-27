#pragma once
#include "whas/core/element.h"
#include <utility>
#include <vector>
#include <raylib.h>

// Something spell projectiles can hit that isn't part of the grid
struct Hurtbox {
    int id;
    Rectangle bounds; // cells
};

struct ParticleHit {
    int targetId;
    int ownerId;
    float power;
    Element element; // what hit: water puts out burning characters
};

// A particle spawn queued by a worker thread (see ElementContext)
struct PendingSpawn {
    Vector2 pos;
    Vector2 vel;
    Element element;
};

struct Particle {
    Vector2 pos;
    Vector2 vel;
    Element element;
    bool active = false;
    // Spell projectiles fly straight until remainingDistance runs out and are
    // the only particles allowed to break cells (spending power per cell).
    bool isProjectile = false;
    float remainingDistance = 0.0f;
    float power = 0.0f;
    int owner = -1; // hurtbox id that cast it; never hits its owner
    float temperature = 0.0f; // heat it lands with; 0 keeps the element default
};

class ParticleSystem {
public:
    ParticleSystem(int maxParticles = 6000);

    // Returns the new particle, or nullptr when the pool is full
    Particle *Spawn(Vector2 pos, Vector2 vel, Element element,
                    float remainingDistance = 0.0f, float power = 0.0f,
                    bool isProjectile = false, int owner = -1);
    // Spawn now, or queue it when called from a worker thread
    static void SpawnFrom(struct ElementContext &ctx, Vector2 pos, Vector2 vel,
                          Element element);
    void Update(struct Grid& grid, struct ElementContext& ctx, float dt);
    void Draw();
    void Clear();

    template <typename Fn> void ForEachActive(Fn &&fn) {
        for (auto &p : m_particles)
            if (p.active)
                fn(p);
    }

    // Whole pool, for snapshots
    std::vector<Particle> &Pool() { return m_particles; }
    const std::vector<Particle> &Pool() const { return m_particles; }

    void SetHurtboxes(std::vector<Hurtbox> hurtboxes) { m_hurtboxes = std::move(hurtboxes); }
    // Hits recorded since the last call
    std::vector<ParticleHit> TakeHits() { return std::exchange(m_hits, {}); }

private:
    bool HitHurtbox(const Particle &p);

    std::vector<Particle> m_particles;
    std::vector<Hurtbox> m_hurtboxes;
    std::vector<ParticleHit> m_hits;
    int m_maxParticles;
};
