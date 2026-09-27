#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace {

constexpr float DT = 1.0f / 60.0f;

Spell MakeSpell(const char *sigil, float sigilScale, int signs) {
  Spell spell;
  spell.name = sigil;
  spell.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, sigilScale, 0.0f});
  for (int i = 0; i < signs; ++i)
    spell.glyphs.push_back({"column", GlyphKind::Sign, {0, -100}, 1.0f, 0.0f});
  return spell;
}

// A busy scene that exercises every element system, spells and rigid bodies
void BuildScene(Simulation &sim) {
  for (int x = 0; x < GRID_W; x += 3)
    sim.Paint(x, GRID_H - 4, Element::ROCK, 3);
  sim.Paint(60, 120, Element::EARTH, 10);
  sim.Paint(120, 60, Element::SAND, 12);
  sim.Paint(200, 50, Element::WATER, 14);
  sim.Paint(260, 140, Element::ICE, 6);
  sim.Paint(160, 140, Element::FIRE, 5);
  sim.Paint(280, 40, Element::STEAM, 6);
  // A burning grove: grass on the floor and a wooden pillar next to the fire
  for (int x = 140; x < 200; ++x)
    sim.Paint(x, GRID_H - 8, Element::GRASS, 0);
  for (int y = GRID_H - 30; y < GRID_H - 8; ++y)
    sim.Paint(170, y, Element::WOOD, 1);
}

// Runs the scene and returns the state hash after every frame
std::vector<uint64_t> Run(int threads, uint64_t seed, int frames) {
  Simulation sim(threads);
  sim.SetSeed(seed);
  BuildScene(sim);

  std::vector<uint64_t> hashes;
  for (int f = 0; f < frames; ++f) {
    if (f == 30)
      sim.CastSpell(MakeSpell("water", 1.0f, 2), {40, 100}, {1, 0});
    if (f == 60)
      sim.CastSpell(MakeSpell("rock", 1.5f, 3), {300, 100}, {-1, 0.2f});
    if (f == 90)
      sim.CastSpell(MakeSpell("gust", 1.0f, 2), {100, 140}, {1, -0.3f});
    if (f == 120)
      sim.CastSpell(MakeSpell("fire", 1.0f, 1), {200, 120}, {0, 1});
    sim.Update(DT);
    hashes.push_back(sim.StateHash());
  }
  return hashes;
}

} // namespace

TEST_CASE("same seed gives the same world on a rerun", "[determinism]") {
  auto a = Run(SIM_THREADS, 1234, 240);
  auto b = Run(SIM_THREADS, 1234, 240);
  for (size_t i = 0; i < a.size(); ++i) {
    INFO("frame " << i);
    REQUIRE(a[i] == b[i]);
  }
}

TEST_CASE("worker thread count does not change the world", "[determinism]") {
  auto one = Run(1, 99, 240);
  auto many = Run(SIM_THREADS, 99, 240);
  auto odd = Run(3, 99, 240);
  for (size_t i = 0; i < one.size(); ++i) {
    INFO("frame " << i);
    REQUIRE(one[i] == many[i]);
    REQUIRE(one[i] == odd[i]);
  }
}

TEST_CASE("different seeds diverge", "[determinism]") {
  auto a = Run(SIM_THREADS, 1, 120);
  auto b = Run(SIM_THREADS, 2, 120);
  REQUIRE(a.back() != b.back());
}
