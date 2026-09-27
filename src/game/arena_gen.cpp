#include "whas/game/arena_gen.h"
#include "whas/constants.h"
#include "whas/core/det_rng.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include <algorithm>
#include <vector>

namespace ArenaGen {

namespace {

constexpr int BASE_SURFACE = 150;  // average ground height (row)
constexpr int HILL_SPACING = 32;   // cells between noise control points
constexpr int HILL_UP = 18;        // tallest hill above the base
constexpr int HILL_DOWN = 6;       // deepest dip below it
constexpr int SPAWN_MARGIN = 40;   // spawn distance from the screen edge
constexpr int SPAWN_FLAT = 8;      // flat ground either side of a spawn
constexpr int TREE_CLEARANCE = 24; // no trees this close to a spawn

// Smooth integer interpolation between control points (smoothstep in 1/256)
int Interp(int a, int b, int t256) {
  int s = t256 * t256 * (3 * 256 - 2 * t256) / (256 * 256);
  return a + (b - a) * s / 256;
}

void Set(Simulation &sim, int x, int y, Element e) {
  if (x >= 0 && x < GRID_W && y >= 0 && y < FLOOR_BOTTOM + 1)
    sim.Paint(x, y, e, 0);
}

} // namespace

Arena Generate(Simulation &sim, uint64_t seed) {
  DetRng rng(seed, 0xA7E4A);

  // Hills: random heights at control points, smoothed in between
  int points = GRID_W / HILL_SPACING + 2;
  std::vector<int> control(points);
  for (int &c : control)
    c = BASE_SURFACE - static_cast<int>(rng.Below(HILL_UP + HILL_DOWN + 1)) +
        HILL_DOWN;

  std::vector<int> surface(GRID_W);
  for (int x = 0; x < GRID_W; ++x) {
    int i = x / HILL_SPACING;
    int t = (x % HILL_SPACING) * 256 / HILL_SPACING;
    surface[x] = Interp(control[i], control[i + 1], t);
  }

  // Which side each player starts on
  int leftSlot = static_cast<int>(rng.Below(2));
  std::array<int, 2> spawnX{};
  spawnX[leftSlot] = SPAWN_MARGIN;
  spawnX[1 - leftSlot] = GRID_W - SPAWN_MARGIN - static_cast<int>(Character::WIDTH);

  // Flatten the ground under each spawn
  for (int sx : spawnX) {
    int h = surface[sx];
    for (int x = std::max(0, sx - SPAWN_FLAT);
         x < std::min(GRID_W, sx + SPAWN_FLAT + 4); ++x)
      surface[x] = h;
  }

  // Ground: earth over a rock bed
  for (int x = 0; x < GRID_W; ++x) {
    for (int y = surface[x]; y <= FLOOR_BOTTOM; ++y)
      Set(sim, x, y, y >= FLOOR_BOTTOM - 3 ? Element::ROCK : Element::EARTH);
  }

  // Rock outcrops poking out of the hills
  int outcrops = 1 + static_cast<int>(rng.Below(3));
  for (int i = 0; i < outcrops; ++i) {
    int cx = 70 + static_cast<int>(rng.Below(GRID_W - 140));
    int r = 3 + static_cast<int>(rng.Below(4));
    int cy = surface[cx] - r / 2;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (dx * dx + dy * dy <= r * r)
          Set(sim, cx + dx, cy + dy, Element::ROCK);
  }

  // Grass in patches along the surface (drawn over outcrops' tops too)
  bool grassy = true;
  for (int x = 0; x < GRID_W; ++x) {
    if (rng.Below(24) == 0)
      grassy = !grassy;
    if (!grassy)
      continue;
    int top = surface[x];
    while (top > 0 && sim.GetCell(x, top - 1).element != Element::AIR)
      --top;
    int blades = 1 + static_cast<int>(rng.Below(3));
    for (int b = 1; b <= blades; ++b)
      Set(sim, x, top - b, Element::GRASS);
  }

  // Trees: a wooden trunk with a grass canopy, kept away from the spawns
  int trees = 1 + static_cast<int>(rng.Below(3));
  for (int i = 0; i < trees; ++i) {
    int x = 50 + static_cast<int>(rng.Below(GRID_W - 100));
    bool nearSpawn = false;
    for (int sx : spawnX)
      nearSpawn |= std::abs(x - sx) < TREE_CLEARANCE;
    if (nearSpawn)
      continue;
    int height = 12 + static_cast<int>(rng.Below(10));
    int base = surface[x];
    for (int y = base - height; y < base; ++y) {
      Set(sim, x, y, Element::WOOD);
      Set(sim, x + 1, y, Element::WOOD);
    }
    int canopy = 5 + static_cast<int>(rng.Below(3));
    int cy = base - height;
    for (int dy = -canopy; dy <= canopy / 2; ++dy)
      for (int dx = -canopy - 1; dx <= canopy + 2; ++dx)
        if (dx * dx + 2 * dy * dy <= canopy * canopy &&
            sim.GetCell(std::clamp(x + dx, 0, GRID_W - 1),
                        std::clamp(cy + dy, 0, GRID_H - 1))
                    .element == Element::AIR)
          Set(sim, x + dx, cy + dy, Element::GRASS);
  }

  Arena arena;
  for (int slot = 0; slot < 2; ++slot) {
    int sx = spawnX[slot];
    int top = surface[sx];
    for (int x = sx; x < sx + static_cast<int>(Character::WIDTH) + 1; ++x)
      top = std::min(top, surface[x]);
    arena.spawns[slot] = {static_cast<float>(sx),
                          static_cast<float>(top) - Character::HEIGHT};
  }
  return arena;
}

} // namespace ArenaGen
