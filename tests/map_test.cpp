#include "whas/core/config_json.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include "whas/game/map.h"
#include <catch2/catch_test_macros.hpp>

namespace {

MapDef GeneratedMap(uint64_t seed) {
  Simulation sim;
  ArenaGen::Arena arena = ArenaGen::Generate(sim, seed);
  MapDef map;
  map.id = "test";
  map.name = "Test map";
  map.genSeed = seed;
  map.gen.biome = arena.biome;
  map.spawns = arena.spawns;
  map.cells = Maps::CaptureCells(sim);
  return map;
}

} // namespace

TEST_CASE("config diffs only keep what changed", "[map]") {
  SimulationConfig config;
  REQUIRE(ConfigDiff(config).empty());
  config.cloud.infiniteRain = true;
  config.world.gravity = 0.5f;
  config.elements[static_cast<size_t>(Element::SAND)].density = 3.0f;
  nlohmann::json diff = ConfigDiff(config);
  REQUIRE(diff.size() == 3);
  SimulationConfig back = ConfigFromDiff(diff);
  REQUIRE(back.cloud.infiniteRain);
  REQUIRE(back.world.gravity == 0.5f);
  REQUIRE(back.elements[static_cast<size_t>(Element::SAND)].density == 3.0f);
  REQUIRE(ConfigDiff(back) == diff);
}

TEST_CASE("a map survives its file format", "[map]") {
  MapDef map = GeneratedMap(99);
  map.settings = {{"Weather", {{"skyRain", 2.0}}}};
  MapDef back;
  std::string error;
  REQUIRE(Maps::FromJson(Maps::ToJson(map), back, error));
  REQUIRE(back.cells == map.cells);
  REQUIRE(back.spawns[1].x == map.spawns[1].x);
  REQUIRE(back.Config().cloud.skyRain == 2.0f);
  // Well under what a lobby accepts
  REQUIRE(Maps::ToJson(map).dump().size() < Maps::MAX_ENCODED_BYTES);
}

TEST_CASE("damaged maps are refused", "[map]") {
  nlohmann::json j = Maps::ToJson(GeneratedMap(5));
  MapDef out;
  std::string error;
  auto bad = j;
  bad["cells"] = "AAAA";
  REQUIRE_FALSE(Maps::FromJson(bad, out, error));
  bad = j;
  bad["spawns"][0] = {-5, 10};
  REQUIRE_FALSE(Maps::FromJson(bad, out, error));
}

TEST_CASE("a custom map builds the same world everywhere", "[map]") {
  MapDef map = GeneratedMap(1234);
  map.settings = {{"Weather", {{"skyRain", 1.5}}}};
  Simulation a, b;
  a.Update(1.0f / 60.0f); // a different past
  Maps::Build(a, map, 7);
  Maps::Build(b, map, 7);
  for (int i = 0; i < 30; ++i) {
    a.Update(1.0f / 60.0f);
    b.Update(1.0f / 60.0f);
  }
  REQUIRE(a.StateHash() == b.StateHash());
  REQUIRE(a.GetConfig().cloud.skyRain == 1.5f);
}

TEST_CASE("generator knobs at their defaults change nothing", "[arena]") {
  Simulation a, b;
  ArenaGen::Generate(a, 321, ArenaGen::Biome::Lake);
  ArenaGen::Params params;
  params.biome = ArenaGen::Biome::Lake;
  ArenaGen::Generate(b, 321, params);
  REQUIRE(a.StateHash() == b.StateHash());
  Simulation c;
  params.hills = 150;
  params.waterRise = 6;
  ArenaGen::Generate(c, 321, params);
  REQUIRE(c.StateHash() != a.StateHash());
}
