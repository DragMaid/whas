#pragma once
#include "whas/campaign/campaign.h"
#include "whas/game/character.h"
#include "whas/spell/spell_system.h"
#include <random>
#include <vector>

class Simulation;

// Campaign enemies: a Character body (so burning, wet paper, knockback and
// spell hits work like on the player) and a little brain.
// TODO: each kind reuses the player's sprite for now; give them their own.
namespace Enemies {

constexpr int TEAM = 1;     // enemies' hurtbox side
constexpr int FIRST_ID = 100; // hurtbox ids from here; the player is 1

struct Enemy {
  Campaign::EnemyDef def;
  Character body;
  std::vector<SpellStats> spells; // a mage's, evaluated once
  float castIn = 1.0f;      // seconds until a mage may cast
  float touchIn = 0.0f;     // seconds until it can hurt by touch again
  // Flyers: the path to the player (cell centres) and when to look again
  std::vector<Vector2> path;
  float repathIn = 0.0f;
  Vector2 lastPos{0, 0};
  float stuckFor = 0.0f;
  int wander = 1; // walkers that lost sight of the player pace this way
};

// The room's enemies, freshly spawned (replacing any from before)
void Spawn(const Campaign::RoomDef &room, const Simulation &sim,
           std::vector<Enemy> &out);

// One tick of thinking and moving; casts go straight into the world.
// player is the body they chase and hurt by touching.
void Tick(Enemy &e, Simulation &sim, Character &player, std::mt19937 &rng,
          float dt);

// Whether the straight line between two points (cells) is clear of solids
bool LineOfSight(const Simulation &sim, Vector2 a, Vector2 b);

// A flyer's route around terrain: cell centres from `from` to `to` for a
// body of Character size, empty when there's none (A* on a coarse grid)
std::vector<Vector2> FindPath(const Simulation &sim, Vector2 from, Vector2 to);

void Draw(const Enemy &e);

} // namespace Enemies
