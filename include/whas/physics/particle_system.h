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

// A light mote bursting: everyone within `radius` cells is blinded for
// `time` seconds (the game decides what that looks like)
struct Flash {
    Vector2 pos;
    float radius;
    float time;
    int owner;
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
    // Spell modifiers (see SpellStats)
    float temperatureDelta = 0.0f; // added to the landing temperature
    float hardnessScale = 1.0f;    // landed cell's hardness multiplier
    float crush = 0.0f;   // > 0 grinds what it hits to sand, < 0 reforms sand
    float restore = 0.0f; // resets what it hits to its natural state
    // Light: bursts into a flash instead of landing
    float flashRadius = 0.0f;
    float flashTime = 0.0f;
    // Guidance (see SpellStats): what it chases while it flies
    uint8_t homeTarget = 0; // HomeTarget
    Element homeElement = Element::AIR;
    float homeTurnRate = 0.0f;
    float homeRadius = 0.0f;
    // Sights set: seconds left following the caster's cursor, and how fast
    // it may turn doing it
    float steerTime = 0.0f;
    float steerRate = 0.0f;
};

// Where a caster's cursor is (cells), for sights set
struct Cursor {
    int owner;
    Vector2 pos;
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
    // Casters' cursors this tick; a caster without one steers nothing
    void SetCursors(std::vector<Cursor> cursors) { m_cursors = std::move(cursors); }
    // Hits recorded since the last call
    std::vector<ParticleHit> TakeHits() { return std::exchange(m_hits, {}); }
    // Flashes from the last Update
    std::vector<Flash> TakeFlashes() { return std::exchange(m_flashes, {}); }

private:
    bool HitHurtbox(const Particle &p);
    // A light mote ends in a flash
    void Burst(Particle &p);
    // Turn guided projectiles toward what they chase
    void Steer(const struct Grid &grid, float dt);

    std::vector<Particle> m_particles;
    std::vector<Hurtbox> m_hurtboxes;
    std::vector<Cursor> m_cursors;
    std::vector<ParticleHit> m_hits;
    std::vector<Flash> m_flashes;
    // Recent flashes for drawing only (not simulation state): where, how
    // big, and how long ago in seconds
    struct VisualFlash {
        Vector2 pos;
        float radius;
        float age;
    };
    std::vector<VisualFlash> m_visualFlashes;
    int m_maxParticles;
};
