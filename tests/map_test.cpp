#include "whas/core/bytes.h"
#include "whas/core/config_json.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include "whas/game/map.h"
#include "whas/game/match.h"
#include "whas/spell/spell_rules.h"
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

TEST_CASE("rounds rotate through the map pool", "[map]") {
  MatchOptions options;
  MapSpec custom;
  custom.custom = GeneratedMap(42);
  custom.custom->spawns = {Vector2{60, 20}, Vector2{250, 20}};
  options.pool = {custom, MapSpec{}};

  Simulation sim;
  Match::State r0 = Match::BeginRound(sim, 9, 0, &options);
  REQUIRE(r0.characters[0].pos.x == 60);
  REQUIRE(r0.characters[1].pos.x == 250);

  // Round 2 is a generated arena, exactly like a match without options
  Simulation plain;
  Match::State r1 = Match::BeginRound(sim, 9, 1, &options);
  Match::State p1 = Match::BeginRound(plain, 9, 1);
  REQUIRE(Match::Hash(sim, r1) == Match::Hash(plain, p1));

  // Round 3 wraps back to the custom map
  REQUIRE(options.MapFor(2) == &options.pool[0]);

  // Options survive the wire
  MatchOptions back;
  std::string error;
  REQUIRE(OptionsFromJson(OptionsToJson(options), back, error));
  REQUIRE(back.pool.size() == 2);
  REQUIRE(back.pool[0].custom);
  REQUIRE_FALSE(back.pool[1].custom);
}

TEST_CASE("signs are counted per circle against the limit", "[spell]") {
  Spell spell;
  spell.glyphs.push_back({"water", GlyphKind::Sigil});
  for (int i = 0; i < SIGN_LIMIT; ++i)
    spell.glyphs.push_back({"levitation", GlyphKind::Sign});
  SpellRules::Count count = SpellRules::CountGlyphs(spell);
  REQUIRE(count.mostSigns == SIGN_LIMIT);
  REQUIRE(count.sigils == 1);
  REQUIRE(SpellRules::WithinLimits(spell));
  spell.glyphs.push_back({"levitation", GlyphKind::Sign});
  REQUIRE_FALSE(SpellRules::WithinLimits(spell));

  // A layered spell: the limit is per circle, not for the whole spell
  Spell layered;
  layered.glyphs.assign(20, {"levitation", GlyphKind::Sign});
  SpellComponent part;
  part.glyphs.assign(20, {"orb", GlyphKind::Sign});
  layered.components = {part, part};
  count = SpellRules::CountGlyphs(layered);
  REQUIRE(count.signs == 60);
  REQUIRE(count.mostSigns == 20);
  REQUIRE_FALSE(count.OverLimit());
  // More than five spells in a layer is over the limit too, but still a spell
  layered.components.assign(LAYER_MAX_COMPONENTS + 1, part);
  REQUIRE(SpellRules::CountGlyphs(layered).OverLimit());
}

TEST_CASE("maps made at an older world size are stretched to this one",
          "[map]") {
  // A 1280x720-era map (320x180 cells, no "size"): rock below row 150
  nlohmann::json j = Maps::ToJson(GeneratedMap(3));
  j.erase("size");
  j["spawns"] = {{40.0, 130.0}, {270.0, 130.0}};

  // Encode 320x180 directly: runs of (count, element)
  std::vector<uint8_t> rle;
  auto run = [&](size_t n, Element e) {
    for (; n >= 0x80; n >>= 7)
      rle.push_back(static_cast<uint8_t>(n & 0x7F) | 0x80);
    rle.push_back(static_cast<uint8_t>(n));
    rle.push_back(static_cast<uint8_t>(e));
  };
  run(150 * 320, Element::AIR);
  run(30 * 320, Element::ROCK);
  j["cells"] = Base64::Encode(rle);

  MapDef back;
  std::string error;
  REQUIRE(Maps::FromJson(j, back, error));
  REQUIRE(back.cells.size() == static_cast<size_t>(GRID_W) * GRID_H);
  int floorRow = 150 * GRID_H / 180;
  REQUIRE(back.cells[static_cast<size_t>(floorRow + 1) * GRID_W + 10] ==
          static_cast<uint8_t>(Element::ROCK));
  REQUIRE(back.cells[static_cast<size_t>(floorRow - 2) * GRID_W + 10] ==
          static_cast<uint8_t>(Element::AIR));
  REQUIRE(back.spawns[0].x == 40.0f * GRID_W / 320);
  // Saved again, it's at this size
  REQUIRE(Maps::ToJson(back)["size"][0] == GRID_W);
}
