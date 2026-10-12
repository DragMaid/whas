#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include "whas/game/character.h"
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr float DT = 1.0f / 60.0f;

int Count(const Simulation &sim, Element e) {
  int n = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      n += sim.GetCell(x, y).element == e;
  return n;
}

void Step(Simulation &sim, int frames) {
  for (int i = 0; i < frames; ++i)
    sim.Update(DT);
}

} // namespace

TEST_CASE("acid eats the earth it sits on and spends itself", "[acid]") {
  Simulation sim;
  sim.SetSeed(3);
  for (int x = 100; x < 140; ++x)
    for (int y = GRID_H - 30; y < GRID_H; ++y)
      sim.Paint(x, y, Element::EARTH, 0);
  for (int x = 110; x < 130; ++x)
    for (int y = GRID_H - 34; y < GRID_H - 30; ++y)
      sim.Paint(x, y, Element::ACID, 0);
  int earth = Count(sim, Element::EARTH), acid = Count(sim, Element::ACID);
  REQUIRE(acid > 0);
  Step(sim, 600);
  REQUIRE(Count(sim, Element::EARTH) < earth);
  REQUIRE(Count(sim, Element::ACID) < acid);
}

TEST_CASE("standing in acid hurts", "[acid]") {
  Simulation sim;
  for (int x = 0; x < GRID_W; ++x)
    for (int y = GRID_H - 4; y < GRID_H; ++y)
      sim.Paint(x, y, Element::ROCK, 0);
  Character c;
  c.pos = {100, GRID_H - 4 - Character::HEIGHT};
  float hp = c.hp;
  c.UpdateBurn(sim, 1.0f);
  REQUIRE(c.hp == hp);
  for (int x = 98; x < 112; ++x)
    for (int y = GRID_H - 8; y < GRID_H - 4; ++y)
      sim.Paint(x, y, Element::ACID, 0);
  c.UpdateBurn(sim, 1.0f);
  REQUIRE(c.hp < hp);
}

TEST_CASE("mire arenas have acid pools and the classic seeds don't", "[acid]") {
  Simulation sim;
  ArenaGen::Generate(sim, 42, ArenaGen::Biome::Mire);
  REQUIRE(Count(sim, Element::ACID) > 50);
  for (ArenaGen::Biome b : {ArenaGen::Biome::Meadow, ArenaGen::Biome::Lake}) {
    Simulation other;
    ArenaGen::Generate(other, 42, b);
    REQUIRE(Count(other, Element::ACID) == 0);
  }
  // Some seeds roll a mire
  int mires = 0;
  for (uint64_t seed = 1; seed <= 60; ++seed) {
    Simulation s;
    mires += ArenaGen::Generate(s, seed).biome == ArenaGen::Biome::Mire;
  }
  REQUIRE(mires > 0);
  REQUIRE(mires < 30);
}
