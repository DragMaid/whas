#pragma once
#include <array>
#include <cstdint>
#include <raylib.h>

class Simulation;

// Builds a duel arena from a seed with integer-only noise, so every client
// paints the exact same cells. The seed also picks the map: rolling meadow
// hills, a mountain between the players, a lake in a sandy basin or a desert
// canyon. All of them stand on deep earth over a rock bed.
namespace ArenaGen {

// Arena terrain stays above this row; the bottom of the screen is the
// toolbar (only the rock bed carries on below it)
constexpr int FLOOR_BOTTOM = 164;

enum class Biome : uint8_t {
  Meadow,   // rolling grassy hills, trees and rock outcrops
  Mountain, // an earth-skinned rock peak between the spawns
  Lake,     // grassy banks around a water-filled sandy basin
  Canyon,   // sandy plateaus split by a deep gorge with a stream
  COUNT
};

const char *BiomeName(Biome biome);

struct Arena {
  std::array<Vector2, 2> spawns; // character top-left, in cells
  Biome biome = Biome::Meadow;
};

// Paints into an empty world (call Simulation::Reset first). The first form
// picks the map from the seed.
Arena Generate(Simulation &sim, uint64_t seed);
Arena Generate(Simulation &sim, uint64_t seed, Biome biome);

} // namespace ArenaGen
