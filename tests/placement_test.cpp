#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/placement.h"
#include "whas/game/turn_controller.h"
#include "whas/net/plan_codec.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

void Floor(Simulation &sim) {
  for (int x = 0; x < GRID_W; ++x)
    for (int y = GRID_H - 4; y < GRID_H; ++y)
      sim.Paint(x, y, Element::ROCK, 0);
}

Spell Fire() {
  Spell s;
  s.name = "fire";
  s.glyphs = {{"fire", GlyphKind::Sigil, {0, 0}, 1.0f, 0}};
  return s;
}

} // namespace

TEST_CASE("placing finds the floor and a wall near the cursor", "[placement]") {
  Simulation sim;
  Floor(sim);
  for (int y = GRID_H - 60; y < GRID_H - 4; ++y)
    sim.Paint(130, y, Element::ROCK, 0);
  Vector2 caster{100, GRID_H - 10.0f};

  auto floor = Placement::FindSurface(sim, {105, GRID_H - 6.0f}, caster);
  REQUIRE(floor);
  REQUIRE(floor->pos.y == Catch::Approx(GRID_H - 4.5f));
  REQUIRE(floor->normal.y < -0.9f);

  auto wall = Placement::FindSurface(sim, {127, GRID_H - 30.0f}, caster);
  REQUIRE(wall);
  REQUIRE(wall->pos.x == Catch::Approx(129.5f));
  REQUIRE(wall->normal.x < -0.9f);

  // Nothing to draw on in open air, and nothing out of reach
  REQUIRE_FALSE(Placement::FindSurface(sim, {100, 40}, caster));
  REQUIRE_FALSE(
      Placement::FindSurface(sim, {100 + Placement::REACH + 20, GRID_H - 5.0f},
                             caster));
}

TEST_CASE("a placed cast survives the wire and fires from its spot",
          "[placement]") {
  Simulation sim;
  Floor(sim);
  Character c;
  c.id = 1;
  c.pos = {60, GRID_H - 4 - Character::HEIGHT};
  c.Step(sim, {}, 0.0f);

  PlannedCast cast = PlannedCast::Local(Fire(), {0, -1});
  cast.PlaceAt({90.5f, GRID_H - 4.5f}, c.Center(), {0.0f, -1.0f});
  TurnPlan plan;
  plan.steps.push_back({});
  plan.steps[0].casts.push_back(cast);

  TurnPlan back;
  std::string error;
  auto resolve = [](int64_t, Spell &spell, SpellStats &stats) {
    spell = Fire();
    stats = SpellQuant::Canonical(spell);
    return true;
  };
  REQUIRE(PlanCodec::Decode(PlanCodec::Encode(plan), resolve, back, error));
  const PlannedCast &got = back.steps[0].casts[0];
  REQUIRE(got.placed);
  Vector2 at = got.Origin(c.Center());
  REQUIRE(at.x == Catch::Approx(90.5f).margin(0.13f));
  REQUIRE(at.y == Catch::Approx(GRID_H - 4.5f).margin(0.13f));

  TurnController::ApplyPlanTick(back, 0, sim, c);
  REQUIRE(sim.GetActiveSpellEffects().size() == 1);
  REQUIRE(sim.GetActiveSpellEffects()[0].origin.x ==
          Catch::Approx(90.5f).margin(0.13f));

  // Too far away is refused
  nlohmann::json j = PlanCodec::Encode(plan);
  j["runs"][0]["casts"][0]["px"] = 2000;
  REQUIRE_FALSE(PlanCodec::Decode(j, resolve, back, error));
}
