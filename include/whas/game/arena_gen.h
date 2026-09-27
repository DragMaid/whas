#pragma once
#include <array>
#include <cstdint>
#include <raylib.h>

class Simulation;

// Builds a duel arena from a seed with integer-only noise, so every client
// paints the exact same cells: rolling earth hills, grass on the surface,
// rock outcrops and a few wooden trees or posts.
namespace ArenaGen {

// Arena cells stay above this row; the bottom of the screen is the toolbar
constexpr int FLOOR_BOTTOM = 164;

struct Arena {
  std::array<Vector2, 2> spawns; // character top-left, in cells
};

// Paints into an empty world (call Simulation::Reset first)
Arena Generate(Simulation &sim, uint64_t seed);

} // namespace ArenaGen
