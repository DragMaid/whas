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

// Something that happened in the world worth hearing: a spell leaving its
// caster, a spell's element striking, a solid breaking, fire meeting water.
// Sound only: not simulation state, never hashed or saved.
struct ParticleNoise {
  enum Kind : uint8_t { Cast, Impact, Break, Fizzle };
  Kind kind;
  Element element; // the spell's element, or what broke
  Vector2 pos;     // cells
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
    // Steered spells (sights set, guidance): the Guide the particle follows
    // (-1 = none), how far along its path the particle is (cells) and how
    // far to the side of it (cells, left positive)
    int guideId = -1;
    float pathS = 0.0f;
    float pathL = 0.0f;
    // Which cast a spell particle belongs to (-1 = not a spell's). Flying
    // spell particles of different casts collide; one cast's never do, so a
    // figure doesn't knock itself apart.
    int castId = -1;
    // Hurtbox this particle last struck: fire passes through bodies and
    // shouldn't burn the same one again on every step inside it
    int lastHit = -1;
};

// Where a caster's cursor is (cells), for sights set
struct Cursor {
    int owner;
    Vector2 pos;
};

// The path a steered cast flies along. Its tip turns toward the cursor
// (sights set) or the target (guidance) and is laid down a cell at a time
// just ahead of the leading particle. Every particle of the cast follows it
// at its own distance along and offset across, so the figure keeps its
// shape: a stream or dragon snakes after its head, an orb stays round.
struct Guide {
    int id = 0;
    int owner = -1;
    float speed = 0.0f; // the spell's cells/s
    // Sights set: seconds left following the cursor, and the turn rate
    float steerTime = 0.0f;
    float steerRate = 0.0f;
    // Guidance (see SpellStats), after the sights set
    uint8_t homeTarget = 0; // HomeTarget
    Element homeElement = Element::AIR;
    float homeTurnRate = 0.0f;
    float homeRadius = 0.0f;
    float heading = 0.0f;      // radians, where the tip is going
    std::vector<Vector2> line; // the path, one point per cell from the cast
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
    // Noises since the last call. Capped, so nobody listening costs nothing.
    void Note(const ParticleNoise &noise) {
        if (m_noises.size() < 1024)
            m_noises.push_back(noise);
    }
    std::vector<ParticleNoise> TakeNoises() { return std::exchange(m_noises, {}); }
    // Light bursts ever, and where the latest was; for sound, never hashed
    uint32_t BurstCount() const { return m_burstCount; }
    Vector2 LastBurstPos() const { return m_lastBurstPos; }

    // A path for a steered cast fired from `origin` along `dir`; its id goes
    // on the cast's particles (see Follow)
    int CreateGuide(const struct SpellStats &stats, Vector2 origin,
                    Vector2 dir, int owner);
    // Put a freshly spawned particle on its guide's path, where it stands
    void Follow(Particle &p, int guideId);

    // A new id for a cast's particles (see Particle::castId)
    int NewCastId() { return m_nextCastId++; }

    // For snapshots
    int &NextCastId() { return m_nextCastId; }
    int NextCastId() const { return m_nextCastId; }
    std::vector<Guide> &Guides() { return m_guides; }
    const std::vector<Guide> &Guides() const { return m_guides; }
    int &NextGuideId() { return m_nextGuideId; }
    int NextGuideId() const { return m_nextGuideId; }

private:
    // Records a hit; true when the particle is spent by it
    bool HitHurtbox(Particle &p);
    // A light mote ends in a flash
    void Burst(Particle &p);
    // Turn the guides toward what they chase and keep their particles on
    // them; drop guides nothing follows any more
    void Steer(const struct Grid &grid, float dt);
    Guide *FindGuide(int id);
    // Spell particles of different casts that meet this tick
    void Collide(struct ElementContext &ctx, float dt);
    // A drop taken into a bigger water spell: it flies on as part of it
    void Absorb(Particle &drop, const Particle &into);

    std::vector<Particle> m_particles;
    std::vector<Guide> m_guides;
    int m_nextGuideId = 0;
    int m_nextCastId = 0;
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
    uint32_t m_burstCount = 0;
    std::vector<ParticleNoise> m_noises;
    Vector2 m_lastBurstPos{0.0f, 0.0f};
    int m_maxParticles;
};
