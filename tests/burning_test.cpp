#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr float DT = 1.0f / 60.0f;

int Count(const Simulation &sim, Element e, uint8_t flag = 0) {
  int n = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &c = sim.GetCell(x, y);
      if (c.element == e && (flag == 0 || (c.flags & flag)))
        ++n;
    }
  return n;
}

void Floor(Simulation &sim) {
  for (int x = 0; x < GRID_W; ++x)
    for (int y = GRID_H - 4; y < GRID_H; ++y)
      sim.Paint(x, y, Element::ROCK, 0);
}

void Step(Simulation &sim, int frames) {
  for (int i = 0; i < frames; ++i)
    sim.Update(DT);
}

} // namespace

TEST_CASE("fire spreads along grass and burns it away", "[burning]") {
  Simulation sim;
  sim.SetSeed(7);
  Floor(sim);
  for (int x = 20; x < 120; ++x)
    sim.Paint(x, GRID_H - 5, Element::GRASS, 0);
  int grass = Count(sim, Element::GRASS);
  REQUIRE(grass == 100);

  sim.Paint(19, GRID_H - 6, Element::FIRE, 1);
  Step(sim, 60 * 11);

  // Most of the strip caught and burnt away, not just the end by the fire
  REQUIRE(Count(sim, Element::GRASS) < grass / 4);
}

TEST_CASE("wood ignites, burns for a while and leaves smoke", "[burning]") {
  Simulation sim;
  sim.SetSeed(3);
  Floor(sim);
  for (int y = GRID_H - 24; y < GRID_H - 4; ++y)
    for (int x = 100; x < 104; ++x)
      sim.Paint(x, y, Element::WOOD, 0);
  int wood = Count(sim, Element::WOOD);

  sim.Paint(99, GRID_H - 6, Element::FIRE, 1);
  Step(sim, 60 * 2);
  REQUIRE(Count(sim, Element::WOOD, CELL_BURNING) > 0);
  REQUIRE(Count(sim, Element::SMOKE) + Count(sim, Element::FIRE) > 0);

  Step(sim, 60 * 25);
  REQUIRE(Count(sim, Element::WOOD) < wood);
}

TEST_CASE("water puts out burning wood", "[burning]") {
  Simulation sim;
  sim.SetSeed(5);
  Floor(sim);
  for (int x = 100; x < 120; ++x)
    sim.Paint(x, GRID_H - 5, Element::WOOD, 0);
  sim.Paint(99, GRID_H - 6, Element::FIRE, 2);
  Step(sim, 60 * 2);
  REQUIRE(Count(sim, Element::WOOD, CELL_BURNING) > 0);

  sim.Paint(110, GRID_H - 12, Element::WATER, 8);
  Step(sim, 60 * 3);
  // Wood under the water is out; any still burning is outside the splash
  int burningUnderWater = 0;
  for (int x = 104; x < 117; ++x) {
    const Cell &c = sim.GetCell(x, GRID_H - 5);
    if (c.element == Element::WOOD && (c.flags & CELL_BURNING))
      ++burningUnderWater;
  }
  REQUIRE(burningUnderWater == 0);
}

TEST_CASE("characters build burn stacks in fire and water clears them",
          "[burning]") {
  Simulation sim;
  sim.SetSeed(1);
  Floor(sim);
  Character c;
  c.pos = {50, GRID_H - 4 - Character::HEIGHT};

  sim.Paint(51, GRID_H - 6, Element::FIRE, 2);
  for (int i = 0; i < 40; ++i)
    c.UpdateBurn(sim, DT);
  REQUIRE(c.burnStacks >= 2);
  REQUIRE(c.hp < c.maxHp);

  sim.Reset();
  Step(sim, 1);
  Floor(sim);
  sim.Paint(51, GRID_H - 6, Element::WATER, 2);
  c.UpdateBurn(sim, DT);
  REQUIRE(c.burnStacks == 0);

  c.burnStacks = 5;
  c.CoolBurn();
  REQUIRE(c.burnStacks == 3);
}


TEST_CASE("a fire bolt sets a body alight and flies on through it",
          "[burning]") {
  Simulation sim;
  sim.SetSeed(2);
  Floor(sim);
  Character c;
  c.id = 1;
  c.pos = {100, GRID_H - 4 - Character::HEIGHT};
  float y = c.Center().y;
  Particle *p = sim.GetParticleSystem().Spawn({90, y}, {120, 0}, Element::FIRE,
                                              60.0f, 50.0f, true, 2);
  REQUIRE(p);
  for (int i = 0; i < 12; ++i) {
    sim.GetParticleSystem().SetHurtboxes({{c.id, c.Bounds()}});
    sim.Update(DT);
    Match::ApplyEffects(sim, &c, 1);
  }
  REQUIRE(c.Burning());
  REQUIRE(c.hp < c.maxHp);
  // Still going, past the body
  REQUIRE(p->active);
  REQUIRE(p->pos.x > c.pos.x + Character::WIDTH);
}

TEST_CASE("water wets the paper: only flight casts until it dries",
          "[burning]") {
  Simulation sim;
  sim.SetSeed(4);
  Floor(sim);
  Character c;
  c.id = 1;
  c.pos = {60, GRID_H - 4 - Character::HEIGHT};
  sim.Paint(63, GRID_H - 8, Element::WATER, 2);
  c.UpdateBurn(sim, DT);
  REQUIRE(c.Wet());
  REQUIRE_FALSE(c.CanCast(false));
  REQUIRE(c.CanCast(true));

  // Fire won't leave wet paper
  Spell fire;
  fire.glyphs = {{"fire", GlyphKind::Sigil, {0, 0}, 1.0f, 0}};
  TurnPlan plan;
  plan.steps.push_back({});
  plan.steps[0].casts.push_back(PlannedCast::Local(fire, {1, 0}));
  REQUIRE(plan.steps[0].casts[0].stats.valid);
  sim.Reset();
  Floor(sim);
  int before = sim.GetActiveSpellEffects().size();
  TurnController::ApplyPlanTick(plan, 0, sim, c);
  REQUIRE((int)sim.GetActiveSpellEffects().size() == before);

  // Drying takes WET_SECONDS on foot, a third of that in flight
  Character walker = c, flyer = c;
  flyer.pos = {200, 10};
  flyer.LaunchFlight({0, -20});
  for (int i = 0; i < 70; ++i) {
    walker.Step(sim, {}, DT);
    flyer.Step(sim, {}, DT);
  }
  REQUIRE_FALSE(flyer.grounded);
  REQUIRE(walker.Wet());
  REQUIRE_FALSE(flyer.Wet());
  for (int i = 0; i < 60 * 3; ++i)
    walker.Step(sim, {}, DT);
  REQUIRE(walker.CanCast(false));
}
