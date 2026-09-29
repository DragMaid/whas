#include <string_view>
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include "whas/game/match.h"
#include "whas/net/plan_codec.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <set>

namespace {

Spell MakeSpell(const char *sigil, float sigilScale, int signs) {
  Spell spell;
  spell.name = sigil;
  spell.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, sigilScale, 0.0f});
  for (int i = 0; i < signs; ++i)
    spell.glyphs.push_back({"column", GlyphKind::Sign, {0, -100}, 1.0f, 0.0f});
  // Wind only moves things: an inverted pulling sign makes it push
  if (std::string_view(sigil) == "wind")
    spell.glyphs.push_back(
        {"pulling", GlyphKind::Sign, {80, 0}, 1.0f, 0.0f, true});
  return spell;
}

// A tiny stand-in for the server: spell ids with their authoritative stats
struct Library {
  std::map<int64_t, Spell> spells;

  PlannedCast Cast(int64_t id, Vector2 aim) const {
    PlannedCast cast = PlannedCast::Local(spells.at(id), aim);
    cast.spellId = id;
    return cast;
  }

  PlanCodec::SpellResolver Resolver() const {
    return [this](int64_t id, Spell &spell, SpellStats &stats) {
      auto it = spells.find(id);
      if (it == spells.end())
        return false;
      spell = it->second;
      stats = SpellQuant::Canonical(spell);
      return true;
    };
  }
};

// Walk toward the middle, then cast
TurnPlan MakePlan(const Library &lib, int turn, int slot) {
  TurnPlan plan;
  bool right = slot == 0;
  for (int t = 0; t < 40; ++t) {
    PlanStep step;
    step.input.right = right;
    step.input.left = !right;
    step.input.jump = t == 10 && turn == 1;
    plan.steps.push_back(step);
  }
  PlanStep cast;
  int64_t id = 1 + (turn + slot) % 4;
  cast.casts.push_back(lib.Cast(id, {right ? 1.0f : -1.0f, -0.15f}));
  plan.steps.push_back(cast);
  return plan;
}

} // namespace

TEST_CASE("the same seed builds the same arena", "[arena]") {
  Simulation a, b, c;
  ArenaGen::Arena arenaA = ArenaGen::Generate(a, 555);
  ArenaGen::Arena arenaB = ArenaGen::Generate(b, 555);
  ArenaGen::Generate(c, 556);
  REQUIRE(a.StateHash() == b.StateHash());
  REQUIRE(a.StateHash() != c.StateHash());
  REQUIRE(arenaA.spawns[0].x == arenaB.spawns[0].x);
  REQUIRE(arenaA.spawns[1].y == arenaB.spawns[1].y);
}

TEST_CASE("players spawn standing on open ground", "[arena]") {
  for (uint64_t seed = 1; seed <= 25; ++seed) {
    Simulation sim;
    Match::State state = Match::BeginRound(sim, seed, 0);
    for (const Character &c : state.characters) {
      INFO("seed " << seed);
      REQUIRE(c.grounded);
      REQUIRE(c.pos.y + Character::HEIGHT <= ArenaGen::FLOOR_BOTTOM);
    }
    REQUIRE(state.characters[0].pos.x != state.characters[1].pos.x);
  }
}

TEST_CASE("both sides of the tie order come up", "[match]") {
  std::set<int> firsts;
  for (uint64_t seed = 0; seed < 32; ++seed)
    firsts.insert(Match::TieOrder(seed)[0]);
  REQUIRE(firsts.size() == 2);
}

TEST_CASE("integer cast time matches the old float formula", "[spell]") {
  for (int particles = 0; particles <= 200; ++particles) {
    // Exact halves (x.5 ticks) now round up; the float version rounded them
    // either way depending on representation error
    if ((3 * particles) % 10 == 5)
      continue;
    SpellStats stats;
    stats.particleCount = particles;
    float seconds = 0.3f + particles * 0.005f;
    int expected = std::max(1, (int)std::round(seconds * 60));
    INFO("particles " << particles);
    REQUIRE(TurnController::CastTicks(stats) == expected);
  }
}

TEST_CASE("quantized stats survive a round trip", "[spell]") {
  for (const char *sigil : {"water", "fire", "rock", "wind", "wind_underfoot", "ice"}) {
    for (int signs = 0; signs < 5; ++signs) {
      SpellQuant::Stats q =
          SpellQuant::Quantize(SpellSystem::Evaluate(MakeSpell(sigil, 1.3f, signs)));
      REQUIRE(SpellQuant::Quantize(SpellQuant::Dequantize(q)) == q);
    }
  }
}

TEST_CASE("plans survive the wire unchanged", "[codec]") {
  Library lib;
  lib.spells[1] = MakeSpell("fire", 1.0f, 2);
  lib.spells[2] = MakeSpell("water", 1.0f, 1);
  lib.spells[3] = MakeSpell("wind", 1.0f, 2);
  lib.spells[4] = MakeSpell("wind_underfoot", 0.8f, 1);

  TurnPlan plan = MakePlan(lib, 1, 0);
  std::string wire = PlanCodec::Encode(plan).dump();
  TurnPlan back;
  std::string error;
  REQUIRE(PlanCodec::Decode(nlohmann::json::parse(wire), lib.Resolver(), back,
                            error));
  REQUIRE(back.steps.size() == plan.steps.size());
  for (size_t i = 0; i < plan.steps.size(); ++i) {
    REQUIRE(back.steps[i].input == plan.steps[i].input);
    REQUIRE(back.steps[i].casts.size() == plan.steps[i].casts.size());
    for (size_t c = 0; c < plan.steps[i].casts.size(); ++c) {
      REQUIRE(back.steps[i].casts[c].aimQ == plan.steps[i].casts[c].aimQ);
      REQUIRE(back.steps[i].casts[c].spellId == plan.steps[i].casts[c].spellId);
    }
  }
  // Runs are collapsed: far fewer entries than steps
  REQUIRE(nlohmann::json::parse(wire)["runs"].size() < 6);

  SECTION("unknown spell ids are rejected") {
    Library empty;
    REQUIRE_FALSE(PlanCodec::Decode(nlohmann::json::parse(wire),
                                    empty.Resolver(), back, error));
  }
}

TEST_CASE("two lockstep peers agree after every turn", "[lockstep]") {
  Library lib;
  lib.spells[1] = MakeSpell("fire", 1.0f, 2);
  lib.spells[2] = MakeSpell("water", 1.0f, 1);
  lib.spells[3] = MakeSpell("wind", 1.0f, 2);
  lib.spells[4] = MakeSpell("rock", 1.2f, 3);

  constexpr uint64_t seed = 0xC0FFEE;
  // Peers run different worker counts, like different machines
  Simulation simA(SIM_THREADS), simB(2);
  Match::State a = Match::BeginRound(simA, seed, 0);
  Match::State b = Match::BeginRound(simB, seed, 0);
  REQUIRE(Match::Hash(simA, a) == Match::Hash(simB, b));

  for (int turn = 0; turn < 4; ++turn) {
    // Each peer plans its own slot; the other's plan arrives as JSON text
    TurnPlan planA0 = MakePlan(lib, turn, 0);
    TurnPlan planB1 = MakePlan(lib, turn, 1);
    std::string error;
    TurnPlan planA1, planB0;
    REQUIRE(PlanCodec::Decode(nlohmann::json::parse(
                                  PlanCodec::Encode(planB1).dump()),
                              lib.Resolver(), planA1, error));
    REQUIRE(PlanCodec::Decode(nlohmann::json::parse(
                                  PlanCodec::Encode(planA0).dump()),
                              lib.Resolver(), planB0, error));

    Match::ExecuteTurn(simA, a, {&planA0, &planA1});
    Match::ExecuteTurn(simB, b, {&planB0, &planB1});
    INFO("turn " << turn);
    REQUIRE(Match::Hash(simA, a) == Match::Hash(simB, b));
  }

  // Something actually happened: the players moved and got hurt
  REQUIRE(a.characters[0].hp + a.characters[1].hp < 2 * Match::MAX_HP);
}

TEST_CASE("later rounds rebuild the arena identically on both peers",
          "[lockstep]") {
  Simulation simA, simB;
  Match::BeginRound(simA, 77, 0);
  Match::BeginRound(simB, 77, 0);
  for (int i = 0; i < 30; ++i) {
    simA.Update(TurnController::TICK_DT);
    simB.Update(TurnController::TICK_DT);
  }
  Match::State a = Match::BeginRound(simA, 77, 1);
  Match::State b = Match::BeginRound(simB, 77, 1);
  for (int i = 0; i < 5; ++i) {
    simA.Update(TurnController::TICK_DT);
    simB.Update(TurnController::TICK_DT);
  }
  REQUIRE(Match::Hash(simA, a) == Match::Hash(simB, b));
}

TEST_CASE("a snapshot brings a desynced peer back into lockstep", "[snapshot]") {
  Library lib;
  lib.spells[1] = MakeSpell("fire", 1.0f, 2);
  lib.spells[2] = MakeSpell("water", 1.0f, 1);
  lib.spells[3] = MakeSpell("wind", 1.0f, 2);
  lib.spells[4] = MakeSpell("rock", 1.2f, 3);

  Simulation simA, simB;
  Match::State a = Match::BeginRound(simA, 4242, 0);
  Match::State b = Match::BeginRound(simB, 4242, 0);
  // Rock chunks break loose so rigid bodies are part of the story
  simA.Paint(150, 100, Element::ROCK, 4);
  simB.Paint(150, 100, Element::ROCK, 4);
  for (int turn = 0; turn < 2; ++turn) {
    TurnPlan p0 = MakePlan(lib, turn, 0), p1 = MakePlan(lib, turn, 1);
    Match::ExecuteTurn(simA, a, {&p0, &p1});
    Match::ExecuteTurn(simB, b, {&p0, &p1});
  }
  // B drifts: one cell differs
  simB.Paint(10, 10, Element::SAND, 0);
  REQUIRE(Match::Hash(simA, a) != Match::Hash(simB, b));

  // A is the reference: both load A's snapshot
  std::string snapshot = Match::EncodeSnapshot(simA, a);
  INFO("snapshot " << snapshot.size() << " bytes");
  REQUIRE(snapshot.size() < 200 * 1024);
  REQUIRE(Match::DecodeSnapshot(snapshot, simB, b));
  REQUIRE(Match::DecodeSnapshot(snapshot, simA, a));
  REQUIRE(Match::Hash(simA, a) == Match::Hash(simB, b));

  // ...and stay together from then on
  for (int turn = 2; turn < 5; ++turn) {
    TurnPlan p0 = MakePlan(lib, turn, 0), p1 = MakePlan(lib, turn, 1);
    Match::ExecuteTurn(simA, a, {&p0, &p1});
    Match::ExecuteTurn(simB, b, {&p0, &p1});
    INFO("turn " << turn);
    REQUIRE(Match::Hash(simA, a) == Match::Hash(simB, b));
  }

  SECTION("garbage is refused and leaves the world alone") {
    uint64_t before = Match::Hash(simB, b);
    REQUIRE_FALSE(Match::DecodeSnapshot("not base64!!", simB, b));
    REQUIRE_FALSE(Match::DecodeSnapshot(snapshot.substr(0, snapshot.size() / 2), simB, b));
    REQUIRE(Match::Hash(simB, b) == before);
  }
}

TEST_CASE("a round starts identically whatever the world did before",
          "[lockstep]") {
  // One peer played in the sandbox before the match (frames, rigid bodies,
  // particles, burning grass); the other starts fresh
  Simulation used, fresh;
  used.Paint(100, 100, Element::ROCK, 6);
  used.Paint(160, 150, Element::WATER, 8);
  used.Paint(200, 170, Element::GRASS, 3);
  used.Paint(200, 165, Element::FIRE, 2);
  for (int i = 0; i < 97; ++i)
    used.Update(TurnController::TICK_DT);

  Match::State a = Match::BeginRound(used, 31337, 0);
  Match::State b = Match::BeginRound(fresh, 31337, 0);
  REQUIRE(Match::Hash(used, a) == Match::Hash(fresh, b));

  TurnPlan idle;
  for (int turn = 0; turn < 2; ++turn) {
    Match::ExecuteTurn(used, a, {&idle, &idle});
    Match::ExecuteTurn(fresh, b, {&idle, &idle});
    INFO("turn " << turn);
    REQUIRE(Match::Hash(used, a) == Match::Hash(fresh, b));
  }
}
