#include "whas/game/arena_gen.h"
#include "whas/constants.h"
#include "whas/core/det_rng.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace ArenaGen {

namespace {

constexpr int ROCK_BED = 4;        // rock rows at the very bottom, at least
constexpr int SPAWN_MARGIN = 40;   // spawn distance from the screen edge
constexpr int SPAWN_FLAT = 8;      // flat ground either side of a spawn
constexpr int TREE_CLEARANCE = 24; // no trees this close to a spawn
constexpr int STRATA_PERIOD = 16;  // canyon walls: a rock band every this many rows
constexpr int NO_WATER = GRID_H;

// Smoothstep in 1/256
int Smooth(int t256) {
  return t256 * t256 * (3 * 256 - 2 * t256) / (256 * 256);
}

// Smooth integer interpolation between control points
int Interp(int a, int b, int t256) { return a + (b - a) * Smooth(t256) / 256; }

// 256 at the centre, falling smoothly to 0 at halfWidth away
int Bump(int x, int centre, int halfWidth) {
  int d = std::abs(x - centre);
  return d >= halfWidth ? 0 : Smooth(256 - d * 256 / halfWidth);
}

// count scaled by a percentage knob
int Scaled(int count, int percent) { return count * percent / 100; }

int Between(DetRng &rng, int lo, int hi) {
  return lo + static_cast<int>(rng.Below(static_cast<uint32_t>(hi - lo + 1)));
}

// Value noise across the width: a random value in [lo, hi] every `spacing`
// cells, smoothed in between
std::vector<int> Noise(DetRng &rng, int spacing, int lo, int hi) {
  std::vector<int> control(GRID_W / spacing + 2);
  for (int &c : control)
    c = Between(rng, lo, hi);
  std::vector<int> out(GRID_W);
  for (int x = 0; x < GRID_W; ++x) {
    int i = x / spacing;
    int t = (x % spacing) * 256 / spacing;
    out[x] = Interp(control[i], control[i + 1], t);
  }
  return out;
}

void Set(Simulation &sim, int x, int y, Element e) {
  if (x >= 0 && x < GRID_W && y >= 0 && y < FLOOR_BOTTOM + 1)
    sim.Paint(x, y, e, 0);
}

bool IsAir(const Simulation &sim, int x, int y) {
  return sim.GetCell(std::clamp(x, 0, GRID_W - 1), std::clamp(y, 0, GRID_H - 1))
             .element == Element::AIR;
}

// What each biome shapes; the painting passes read it
struct Layout {
  std::vector<int> surface = std::vector<int>(GRID_W); // top solid row
  std::vector<int> sandDepth = std::vector<int>(GRID_W, 0);
  // Rock core under an earth skin, rows [surface + coreSkin, coreBottom)
  std::vector<int> coreSkin = std::vector<int>(GRID_W, 0);
  std::vector<int> coreBottom = std::vector<int>(GRID_W, 0);
  std::vector<int> strata; // per-column band offset; empty for none
  std::vector<bool> fertile = std::vector<bool>(GRID_W, true);
  int waterLevel = NO_WATER; // water fills rows from here down to the ground
};

void ShapeMeadow(DetRng &rng, Layout &l) {
  l.surface = Noise(rng, 32, 112, 138);
}

void ShapeMountain(DetRng &rng, Layout &l) {
  std::vector<int> base = Noise(rng, 32, 126, 138);
  std::vector<int> jag = Noise(rng, 9, -4, 4);
  std::vector<int> skin = Noise(rng, 16, 5, 9);

  int centre = GRID_W / 2 + Between(rng, -20, 20);
  int height = Between(rng, 42, 62);
  // Sometimes a saddle of two peaks instead of one
  struct Peak {
    int x, halfWidth, height;
  };
  std::vector<Peak> peaks;
  if (rng.Below(2)) {
    int lead = rng.Below(2) ? 1 : -1;
    peaks.push_back({centre - 34, 52, lead > 0 ? height : height * 2 / 3});
    peaks.push_back({centre + 34, 52, lead > 0 ? height * 2 / 3 : height});
  } else {
    peaks.push_back({centre, Between(rng, 70, 85), height});
  }

  for (int x = 0; x < GRID_W; ++x) {
    int lift = 0;
    int b = 0;
    for (const Peak &p : peaks) {
      int pb = Bump(x, p.x, p.halfWidth);
      lift = std::max(lift, p.height * pb / 256);
      b = std::max(b, pb);
    }
    l.surface[x] = base[x] - lift - jag[x] * b / 256;
    if (lift > 0) {
      l.coreSkin[x] = skin[x];
      l.coreBottom[x] = base[x] - 2;
    }
  }
}

void ShapeLake(DetRng &rng, Layout &l) {
  std::vector<int> bank = Noise(rng, 28, 116, 130);
  std::vector<int> bed = Noise(rng, 12, -2, 2);
  int centre = GRID_W / 2 + Between(rng, -15, 15);
  int halfWidth = Between(rng, 55, 70);
  int floor = Between(rng, 150, 155);

  int highestDry = 0; // lowest bank row outside the basin
  for (int x = 0; x < GRID_W; ++x) {
    int b = Bump(x, centre, halfWidth);
    l.surface[x] = bank[x] + (floor - bank[x]) * b / 256 + bed[x] * b / 256;
    if (b == 0)
      highestDry = std::max(highestDry, bank[x]);
  }
  // Brim just below the banks, so the water stays in the basin
  l.waterLevel = highestDry + 2;
  for (int x = 0; x < GRID_W; ++x) {
    bool shore = l.surface[x] >= l.waterLevel - 3;
    l.sandDepth[x] = shore ? 3 : 0;
    l.fertile[x] = !shore;
  }
}

void ShapeCanyon(DetRng &rng, Layout &l) {
  std::vector<int> plateau = Noise(rng, 40, 100, 110);
  std::vector<int> jag = Noise(rng, 6, -3, 3);
  l.strata = Noise(rng, 48, 0, 5);
  int centre = GRID_W / 2 + Between(rng, -12, 12);
  int halfWidth = Between(rng, 18, 28);
  int floor = Between(rng, 150, 154);
  constexpr int WALL = 6; // cells the cliff face leans over

  for (int x = 0; x < GRID_W; ++x) {
    int d = std::abs(x - centre) + jag[x];
    if (d < halfWidth)
      l.surface[x] = floor;
    else if (d < halfWidth + WALL)
      l.surface[x] = floor + (plateau[x] - floor) * (d - halfWidth) / WALL;
    else
      l.surface[x] = plateau[x];
    // Dunes on the tops, bare earth in the gorge
    l.sandDepth[x] = d >= halfWidth + WALL ? 2 : 0;
    l.fertile[x] = false; // desert
  }
  l.waterLevel = floor - 3; // a stream along the bottom
}

void Outcrops(Simulation &sim, DetRng &rng, const Layout &l, int count) {
  for (int i = 0; i < count; ++i) {
    int cx = 70 + static_cast<int>(rng.Below(GRID_W - 140));
    int r = 3 + static_cast<int>(rng.Below(4));
    int cy = l.surface[cx] - r / 2;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        if (dx * dx + dy * dy <= r * r)
          Set(sim, cx + dx, cy + dy, Element::ROCK);
  }
}

void Grass(Simulation &sim, DetRng &rng, const Layout &l) {
  bool grassy = true;
  for (int x = 0; x < GRID_W; ++x) {
    if (rng.Below(24) == 0)
      grassy = !grassy;
    if (!grassy || !l.fertile[x])
      continue;
    // Drawn over outcrops' tops too
    int top = l.surface[x];
    while (top > 0 && !IsAir(sim, x, top - 1))
      --top;
    int blades = 1 + static_cast<int>(rng.Below(3));
    for (int b = 1; b <= blades; ++b)
      Set(sim, x, top - b, Element::GRASS);
  }
}

// A spot for a tree: fertile (or any dry ground for cacti), away from spawns
int TreeSpot(DetRng &rng, const Layout &l, const std::array<int, 2> &spawnX,
             bool needFertile) {
  int x = 50 + static_cast<int>(rng.Below(GRID_W - 100));
  for (int sx : spawnX)
    if (std::abs(x - sx) < TREE_CLEARANCE)
      return -1;
  if (needFertile && !(l.fertile[x] && l.fertile[x + 1]))
    return -1;
  if (l.surface[x] >= l.waterLevel || l.surface[x + 1] >= l.waterLevel)
    return -1;
  return x;
}

// A wooden trunk with a grass canopy
void Trees(Simulation &sim, DetRng &rng, const Layout &l,
           const std::array<int, 2> &spawnX, int count) {
  for (int i = 0; i < count; ++i) {
    int x = TreeSpot(rng, l, spawnX, true);
    if (x < 0)
      continue;
    int height = 12 + static_cast<int>(rng.Below(10));
    int base = std::min(l.surface[x], l.surface[x + 1]);
    for (int tx : {x, x + 1})
      for (int y = base - height; y < l.surface[tx]; ++y)
        Set(sim, tx, y, Element::WOOD);
    int canopy = 5 + static_cast<int>(rng.Below(3));
    int cy = base - height;
    for (int dy = -canopy; dy <= canopy / 2; ++dy)
      for (int dx = -canopy - 1; dx <= canopy + 2; ++dx)
        if (dx * dx + 2 * dy * dy <= canopy * canopy &&
            IsAir(sim, x + dx, cy + dy))
          Set(sim, x + dx, cy + dy, Element::GRASS);
  }
}

// Wooden cacti on the canyon tops: a trunk and an arm or two
void Cacti(Simulation &sim, DetRng &rng, const Layout &l,
           const std::array<int, 2> &spawnX, int count) {
  for (int i = 0; i < count; ++i) {
    int x = TreeSpot(rng, l, spawnX, false);
    if (x < 0 || l.sandDepth[x] == 0)
      continue;
    int base = std::min(l.surface[x], l.surface[x + 1]);
    int height = 7 + static_cast<int>(rng.Below(6));
    for (int y = base - height; y < base; ++y) {
      Set(sim, x, y, Element::WOOD);
      Set(sim, x + 1, y, Element::WOOD);
    }
    for (int side : {-1, 1}) {
      if (rng.Below(3) == 0)
        continue;
      int armY = base - 3 - static_cast<int>(rng.Below(height / 2));
      int ax = side < 0 ? x - 2 : x + 3;
      Set(sim, ax + (side < 0 ? 1 : -1), armY, Element::WOOD);
      Set(sim, ax, armY, Element::WOOD);
      for (int y = armY - 3; y < armY; ++y)
        Set(sim, ax, y, Element::WOOD);
    }
  }
}

} // namespace

const char *BiomeName(Biome biome) {
  switch (biome) {
  case Biome::Meadow:
    return "Meadow";
  case Biome::Mountain:
    return "Mountain";
  case Biome::Lake:
    return "Lake";
  case Biome::Canyon:
    return "Canyon";
  default:
    return "Unknown";
  }
}

Arena Generate(Simulation &sim, uint64_t seed) {
  DetRng rng(seed, 0xB10E);
  auto biome = static_cast<Biome>(rng.Below(static_cast<uint32_t>(Biome::COUNT)));
  return Generate(sim, seed, biome);
}

Arena Generate(Simulation &sim, uint64_t seed, Biome biome) {
  Params params;
  params.biome = biome;
  return Generate(sim, seed, params);
}

void AnchorRock(Simulation &sim) {
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (sim.GetCell(x, y).element == Element::ROCK)
        sim.Anchor(x, y);
}

Arena Generate(Simulation &sim, uint64_t seed, const Params &params) {
  DetRng rng(seed, 0xA7E4A);
  Biome biome = params.biome;

  Layout l;
  switch (biome) {
  case Biome::Mountain:
    ShapeMountain(rng, l);
    break;
  case Biome::Lake:
    ShapeLake(rng, l);
    break;
  case Biome::Canyon:
    ShapeCanyon(rng, l);
    break;
  default:
    ShapeMeadow(rng, l);
    break;
  }

  // Knobs that don't touch the random stream, so the defaults change nothing
  if (params.hills != 100) {
    int mean = 0;
    for (int h : l.surface)
      mean += h;
    mean /= GRID_W;
    for (int x = 0; x < GRID_W; ++x) {
      int lifted = mean + (l.surface[x] - mean) * params.hills / 100;
      l.coreBottom[x] += lifted - l.surface[x];
      l.surface[x] = std::clamp(lifted, 8, FLOOR_BOTTOM - ROCK_BED - 2);
    }
  }
  if (params.waterRise != 0) {
    int deepest = *std::max_element(l.surface.begin(), l.surface.end());
    int level = l.waterLevel == NO_WATER ? deepest + 1 : l.waterLevel;
    l.waterLevel = std::clamp(level - params.waterRise, 8, NO_WATER);
  }

  // Steep ground stays bare
  for (int x = 0; x < GRID_W; ++x) {
    int rise = l.surface[std::min(x + 2, GRID_W - 1)] -
               l.surface[std::max(x - 2, 0)];
    if (std::abs(rise) > 4)
      l.fertile[x] = false;
  }

  // Which side each player starts on
  int leftSlot = static_cast<int>(rng.Below(2));
  std::array<int, 2> spawnX{};
  spawnX[leftSlot] = SPAWN_MARGIN;
  spawnX[1 - leftSlot] = GRID_W - SPAWN_MARGIN - static_cast<int>(Character::WIDTH);

  // Flatten the ground under each spawn
  for (int sx : spawnX) {
    int h = l.surface[sx];
    for (int x = std::max(0, sx - SPAWN_FLAT);
         x < std::min(GRID_W, sx + SPAWN_FLAT + 4); ++x)
      l.surface[x] = h;
  }

  // Ground: deep earth (sand on top where the biome has it) over a rock bed,
  // with any rock core and strata bands
  std::vector<int> bed = Noise(rng, 20, FLOOR_BOTTOM - ROCK_BED - 5,
                               FLOOR_BOTTOM - ROCK_BED + 1);
  for (int x = 0; x < GRID_W; ++x) {
    for (int y = l.surface[x]; y <= FLOOR_BOTTOM; ++y) {
      int depth = y - l.surface[x];
      Element e = Element::EARTH;
      if (y >= bed[x])
        e = Element::ROCK;
      else if (depth < l.sandDepth[x])
        e = Element::SAND;
      else if (depth >= l.coreSkin[x] && y < l.coreBottom[x])
        e = Element::ROCK;
      else if (!l.strata.empty() && depth > 2 &&
               (y + l.strata[x]) % STRATA_PERIOD < 2)
        e = Element::ROCK;
      Set(sim, x, y, e);
    }
    // The bed runs on down behind the toolbar to the bottom of the grid
    for (int y = FLOOR_BOTTOM + 1; y < GRID_H; ++y)
      sim.Paint(x, y, Element::ROCK, 0);
  }

  // Water over the low ground
  for (int x = 0; x < GRID_W; ++x)
    for (int y = l.waterLevel; y < l.surface[x]; ++y)
      Set(sim, x, y, Element::WATER);

  const int rocks = params.rocks, plants = params.vegetation;
  auto grass = [&] {
    if (plants > 0)
      Grass(sim, rng, l);
  };
  switch (biome) {
  case Biome::Meadow:
    Outcrops(sim, rng, l, Scaled(1 + static_cast<int>(rng.Below(3)), rocks));
    grass();
    Trees(sim, rng, l, spawnX, Scaled(2 + static_cast<int>(rng.Below(3)), plants));
    break;
  case Biome::Mountain:
    Outcrops(sim, rng, l, Scaled(2 + static_cast<int>(rng.Below(3)), rocks));
    grass();
    Trees(sim, rng, l, spawnX, Scaled(1 + static_cast<int>(rng.Below(3)), plants));
    break;
  case Biome::Lake:
    grass();
    Trees(sim, rng, l, spawnX, Scaled(2 + static_cast<int>(rng.Below(3)), plants));
    break;
  case Biome::Canyon:
    Cacti(sim, rng, l, spawnX, Scaled(2 + static_cast<int>(rng.Below(4)), plants));
    break;
  default:
    break;
  }

  // Terrain rock stays put like the earth around it, instead of bodies that
  // settle and wobble (and thump) all round long
  AnchorRock(sim);

  Arena arena;
  arena.biome = biome;
  for (int slot = 0; slot < 2; ++slot) {
    int sx = spawnX[slot];
    int top = l.surface[sx];
    for (int x = sx; x < sx + static_cast<int>(Character::WIDTH) + 1; ++x)
      top = std::min(top, l.surface[x]);
    arena.spawns[slot] = {static_cast<float>(sx),
                          static_cast<float>(top) - Character::HEIGHT};
  }
  return arena;
}

} // namespace ArenaGen
