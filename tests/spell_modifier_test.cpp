#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_json.h"
#include "whas/spell/spell_quant.h"
#include "whas/spell/svg_library.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

constexpr float DT = 1.0f / 60.0f;

int Count(const Simulation &sim, Element e) {
  int n = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      n += sim.GetCell(x, y).element == e;
  return n;
}

void Fill(Simulation &sim, int x0, int y0, int x1, int y1, Element e) {
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
      sim.Paint(x, y, e, 0);
}

void Floor(Simulation &sim) {
  Fill(sim, 0, GRID_H - 4, GRID_W - 1, GRID_H - 1, Element::EARTH);
}

void Step(Simulation &sim, int frames) {
  for (int i = 0; i < frames; ++i)
    sim.Update(DT);
}

// A spell with one sigil, a forward column and the given modifier signs
Spell Make(const char *sigil, std::vector<PlacedGlyph> modifiers = {}) {
  Spell s;
  s.name = sigil;
  s.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f});
  s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 2.0f, 0.0f});
  for (auto &m : modifiers)
    s.glyphs.push_back(m);
  return s;
}

PlacedGlyph Sign(const char *id, float scale, bool inverted = false) {
  return {id, GlyphKind::Sign, {100, 0}, scale, 0.0f, inverted};
}

SpellComponent Part(const Spell &spell, float scale, float rotation = 0.0f) {
  return {spell.name, spell.glyphs, {0, 0}, scale, rotation};
}

} // namespace

TEST_CASE("a character buried in sand shrugs it off where it stands",
          "[character]") {
  Simulation sim;
  sim.SetSeed(3);
  Floor(sim);
  Character c;
  c.pos = {50, GRID_H - 4 - Character::HEIGHT};
  Vector2 start = c.pos;
  // Sand dumped right over the character, up to its head
  Fill(sim, 48, GRID_H - 12, 59, GRID_H - 5, Element::SAND);

  c.Unbury(sim);
  c.Step(sim, {}, DT);
  Rectangle b = c.Bounds();
  for (int y = (int)b.y; y < (int)(b.y + b.height); ++y)
    for (int x = (int)b.x; x < (int)(b.x + b.width); ++x)
      REQUIRE(sim.GetCell(x, y).element == Element::AIR);
  // Not lifted onto the pile: the grains went flying instead
  REQUIRE(c.pos.y >= start.y - 1.0f);
  int flying = 0;
  sim.GetParticleSystem().ForEachActive(
      [&](Particle &p) { flying += p.element == Element::SAND; });
  REQUIRE(flying > 20);
}

TEST_CASE("rock grown into a character slips it aside, never to the top",
          "[character]") {
  Simulation sim;
  Floor(sim);
  Character c;
  c.pos = {50, GRID_H - 4 - Character::HEIGHT};
  // A wall grows two cells into the body from the left, all the way up
  Fill(sim, 30, GRID_H - 60, 51, GRID_H - 5, Element::ROCK);
  for (int y = GRID_H - 60; y <= GRID_H - 5; ++y)
    for (int x = 30; x <= 51; ++x)
      sim.Anchor(x, y);
  c.Step(sim, {}, DT);
  REQUIRE(c.pos.x >= 52.0f);
  REQUIRE(c.pos.x <= 52.0f + Character::UNSTUCK_REACH);
  REQUIRE(c.pos.y > GRID_H - 30.0f);

  // Sealed in: it stays put and has to dig
  Character sealed;
  sealed.pos = {35, GRID_H - 40.0f};
  Vector2 at = sealed.pos;
  sealed.Step(sim, {false, true, true, false}, DT);
  REQUIRE(sealed.pos.x == at.x);
  REQUIRE(sealed.pos.y == at.y);
}

TEST_CASE("an earth crushing spell digs and leaves no earth behind",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(9);
  Fill(sim, 0, GRID_H - 4, GRID_W - 1, GRID_H - 1, Element::ROCK);
  Fill(sim, 150, GRID_H - 60, 175, GRID_H - 5, Element::ROCK);
  for (int y = GRID_H - 60; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (sim.GetCell(x, y).element == Element::ROCK)
        sim.Anchor(x, y);
  int rock = Count(sim, Element::ROCK);
  SpellStats dig = SpellQuant::Canonical(
      Make("earth", {Sign("crushing", 2.0f), Sign("convergence", 2.0f)}));
  REQUIRE(dig.crush > 0.0f);
  sim.CastSpell(dig, {120, GRID_H - 30.0f}, {1, 0});
  Step(sim, 120);
  REQUIRE(Count(sim, Element::EARTH) == 0);
  REQUIRE(Count(sim, Element::ROCK) < rock);
  int earthParticles = 0;
  sim.GetParticleSystem().ForEachActive(
      [&](Particle &p) { earthParticles += p.element == Element::EARTH; });
  REQUIRE(earthParticles == 0);
}

TEST_CASE("crushing grinds earth into sand and inverted crushing reforms it",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(5);
  Floor(sim);
  Fill(sim, 150, GRID_H - 40, 170, GRID_H - 5, Element::EARTH);
  int sand = Count(sim, Element::SAND);

  SpellStats crush =
      SpellQuant::Canonical(Make("water", {Sign("crushing", 2.0f)}));
  REQUIRE(crush.crush > 0.0f);
  sim.CastSpell(crush, {120, GRID_H - 20.0f}, {1, 0});
  Step(sim, 90);
  int crushed = Count(sim, Element::SAND);
  REQUIRE(crushed > sand + 10);
  // It digs: the grit is thrown out of the block, not left where it was
  int thrown = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      thrown += sim.GetCell(x, y).element == Element::SAND &&
                (x < 150 || x >= 170 || y < GRID_H - 40);
  REQUIRE(thrown > 5);

  SpellStats reform =
      SpellQuant::Canonical(Make("water", {Sign("crushing", 2.0f, true)}));
  REQUIRE(reform.crush < 0.0f);
  int earth = Count(sim, Element::EARTH);
  sim.CastSpell(reform, {120, GRID_H - 10.0f}, {1, 0});
  Step(sim, 90);
  REQUIRE(Count(sim, Element::EARTH) > earth);
}

TEST_CASE("pulling signs turn a sigil into a field that moves its element",
          "[spell]") {
  REQUIRE_FALSE(SpellSystem::Evaluate(Make("wind")).valid);
  REQUIRE_FALSE(
      SpellSystem::Evaluate(Make("wind_underfoot", {Sign("pulling", 1.0f)}))
          .valid);

  SpellStats pull =
      SpellQuant::Canonical(Make("wind", {Sign("pulling", 1.0f)}));
  REQUIRE(pull.valid);
  REQUIRE(pull.kind == SpellKind::Field);
  REQUIRE(pull.element == Element::AIR);
  REQUIRE(pull.pull > 0.0f);
  SpellStats push =
      SpellQuant::Canonical(Make("wind", {Sign("pulling", 1.0f, true)}));
  REQUIRE(push.pull < 0.0f);
  REQUIRE(push.force == pull.force);

  SpellStats water =
      SpellQuant::Canonical(Make("water", {Sign("pulling", 1.0f)}));
  REQUIRE(water.kind == SpellKind::Field);
  REQUIRE(water.element == Element::WATER);
  REQUIRE(water.particleCount == 0); // moves water, makes none
}

TEST_CASE("an element's field only pulls that element toward the caster",
          "[spell]") {
  auto meanX = [](const Simulation &sim, Element e) {
    float sum = 0.0f;
    int n = 0;
    for (int y = 0; y < GRID_H; ++y)
      for (int x = 0; x < GRID_W; ++x)
        if (sim.GetCell(x, y).element == e) {
          sum += x;
          n++;
        }
    return n > 0 ? sum / n : 0.0f;
  };
  auto pile = [](Simulation &sim) {
    sim.SetSeed(11);
    Floor(sim);
    Fill(sim, 120, GRID_H - 10, 126, GRID_H - 5, Element::SAND);
    Step(sim, 30); // settle
  };
  Vector2 caster{100, GRID_H - 7.0f};

  Simulation sand;
  pile(sand);
  float before = meanX(sand, Element::SAND);
  sand.CastSpell(
      SpellQuant::Canonical(Make("sand", {Sign("pulling", 2.0f)})), caster,
      {1, 0});
  Step(sand, 60);
  Step(sand, 120); // what was lifted lands again
  REQUIRE(meanX(sand, Element::SAND) < before - 1.0f);

  Simulation water;
  pile(water);
  before = meanX(water, Element::SAND);
  water.CastSpell(
      SpellQuant::Canonical(Make("water", {Sign("pulling", 2.0f)})), caster,
      {1, 0});
  Step(water, 180);
  REQUIRE(std::abs(meanX(water, Element::SAND) - before) < 0.5f);
}

TEST_CASE("light is fast, weightless and blinds whoever it bursts near",
          "[spell]") {
  SpellStats water = SpellQuant::Canonical(Make("water"));
  SpellStats light = SpellQuant::Canonical(Make("light"));
  REQUIRE(light.valid);
  REQUIRE(light.element == Element::LIGHT);
  REQUIRE(light.power == 0.0f);
  REQUIRE(light.speed > water.speed * 2.0f);
  REQUIRE(light.flashRadius > 0.0f);
  REQUIRE(light.flashTime > 0.0f);
  REQUIRE_FALSE(
      SpellSystem::Evaluate(Make("light", {Sign("pulling", 1.0f)})).valid);

  Simulation sim;
  sim.SetSeed(13);
  Floor(sim);
  // A wall past the target so the motes burst there as well
  Fill(sim, 200, GRID_H - 60, 205, GRID_H - 5, Element::EARTH);
  Character caster, target;
  caster.id = 1;
  caster.pos = {60, GRID_H - 4 - Character::HEIGHT};
  target.id = 2;
  target.pos = {190, GRID_H - 4 - Character::HEIGHT};
  Character chars[] = {caster, target};
  sim.GetParticleSystem().SetHurtboxes(
      {{1, chars[0].Bounds()}, {2, chars[1].Bounds()}});
  sim.CastSpell(light, chars[0].Center(), {1, 0}, 1);
  for (int i = 0; i < 120; ++i) {
    sim.Update(DT);
    Match::ApplyEffects(sim, chars, 2);
  }
  REQUIRE(chars[1].flash > 0.0f);
  REQUIRE(chars[1].flash <= 2.0f); // a glare, not a knockout
  REQUIRE(chars[0].flash == 0.0f); // far from every burst
  REQUIRE(Count(sim, Element::LIGHT) == 0); // never lands as a cell
}

namespace {
PlacedGlyph Sigil(const char *id, float scale, Vector2 at) {
  return {id, GlyphKind::Sigil, at, scale, 0.0f};
}
} // namespace

TEST_CASE("guidance fires the bigger sigil at the human or the smaller one",
          "[spell]") {
  Spell atHuman = Make("light", {Sigil("guidance", 1.0f, {0, 120}),
                                 Sigil("human", 0.6f, {-100, 60})});
  SpellStats s = SpellSystem::Evaluate(atHuman);
  REQUIRE(s.valid);
  REQUIRE(s.element == Element::LIGHT);
  REQUIRE(s.homeTarget == HomeTarget::Human);
  REQUIRE(s.homeTurnRate > 0.0f);

  Spell atWater = Make("fire", {Sigil("guidance", 1.0f, {0, 120}),
                                Sigil("water", 0.5f, {-100, 60})});
  s = SpellSystem::Evaluate(atWater);
  REQUIRE(s.valid);
  REQUIRE(s.element == Element::FIRE);
  REQUIRE(s.homeTarget == HomeTarget::Element);
  REQUIRE(s.homeElement == Element::WATER);

  // Two elements without guidance, guidance without a target, or a human
  // without guidance make no spell
  REQUIRE_FALSE(
      SpellSystem::Evaluate(Make("fire", {Sigil("water", 0.5f, {-100, 60})}))
          .valid);
  REQUIRE_FALSE(
      SpellSystem::Evaluate(Make("fire", {Sigil("guidance", 1.0f, {0, 120})}))
          .valid);
  REQUIRE_FALSE(
      SpellSystem::Evaluate(Make("fire", {Sigil("human", 1.0f, {0, 120})}))
          .valid);
}

TEST_CASE("a guided spell curves onto the nearest enemy", "[spell]") {
  Simulation sim;
  sim.SetSeed(17);
  // Aimed straight up, the target off to the right: unguided it misses
  Character target;
  target.id = 2;
  target.pos = {150, 60};
  Spell bolt = Make("water");
  Spell guided = Make("water", {Sigil("guidance", 1.5f, {0, 120}),
                                Sigil("human", 0.5f, {-100, 60})});
  auto hits = [&](const Spell &spell) {
    sim.Reset();
    sim.GetParticleSystem().SetHurtboxes({{2, target.Bounds()}});
    sim.CastSpell(SpellQuant::Canonical(spell), {130, 100}, {0, -1}, 1);
    int n = 0;
    for (int i = 0; i < 90; ++i) {
      sim.Update(DT);
      n += static_cast<int>(sim.GetParticleSystem().TakeHits().size());
    }
    return n;
  };
  REQUIRE(hits(bolt) == 0);
  REQUIRE(hits(guided) > 0);
}

TEST_CASE("sights set steers a spell after the caster's cursor", "[spell]") {
  SpellStats small =
      SpellQuant::Canonical(Make("water", {Sign("sights_set", 0.5f)}));
  SpellStats big =
      SpellQuant::Canonical(Make("water", {Sign("sights_set", 2.0f)}));
  REQUIRE(small.steerTime > 0.0f);
  REQUIRE(big.steerTime > small.steerTime);
  // Hard to turn: well under a full turn a second
  REQUIRE(big.steerRate < 2.0f * PI);

  // Fired straight up with the cursor off to the right
  auto meanX = [](Simulation &sim, const Spell &spell, bool cursor) {
    sim.Reset();
    sim.CastSpell(SpellQuant::Canonical(spell), {100, 150}, {0, -1}, 1);
    for (int i = 0; i < 40; ++i) {
      if (cursor)
        sim.GetParticleSystem().SetCursors({{1, {250, 150}}});
      sim.Update(DT);
    }
    float sum = 0.0f;
    int n = 0;
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      sum += p.pos.x;
      n++;
    });
    return n > 0 ? sum / n : 0.0f;
  };
  Simulation sim;
  sim.SetSeed(19);
  Spell sighted = Make("water", {Sign("sights_set", 2.0f)});
  float straight = meanX(sim, Make("water"), true);
  float steered = meanX(sim, sighted, true);
  float noCursor = meanX(sim, sighted, false);
  REQUIRE(steered > straight + 5.0f);
  REQUIRE(std::abs(noCursor - straight) < 1.0f);
}

TEST_CASE("cooled water lands as ice", "[spell]") {
  Simulation sim;
  sim.SetSeed(7);
  Floor(sim);
  SpellStats cold =
      SpellQuant::Canonical(Make("water", {Sign("cooling", 1.0f)}));
  REQUIRE(cold.element == Element::ICE); // already ice as it flies
  REQUIRE(cold.temperatureDelta < -100.0f);
  sim.CastSpell(cold, {100, GRID_H - 20.0f}, {1, 0});
  Step(sim, 240); // flies its range, then falls and freezes
  REQUIRE(Count(sim, Element::WATER) < 5);
  REQUIRE(Count(sim, Element::ICE) > 10);
}

TEST_CASE("cooling fire makes it cooler, then too cold to ignite",
          "[spell]") {
  SpellStats hot = SpellQuant::Canonical(Make("fire"));
  SpellStats warm =
      SpellQuant::Canonical(Make("fire", {Sign("cooling", 1.0f)}));
  REQUIRE(warm.element == Element::FIRE);
  REQUIRE(warm.temperature < hot.temperature);
  SpellStats cold = SpellQuant::Canonical(
      Make("fire", {Sign("cooling", 2.0f), Sign("cooling", 1.5f)}));
  REQUIRE(cold.element == Element::SMOKE);

  // Earth stays earth, only cold
  SpellStats earth =
      SpellQuant::Canonical(Make("earth", {Sign("cooling", 1.0f)}));
  REQUIRE(earth.element == Element::EARTH);
  REQUIRE(earth.temperatureDelta < 0.0f);
  // Repetition undoes it
  SpellStats restored = SpellQuant::Canonical(
      Make("water", {Sign("cooling", 1.0f), Sign("repetition", 1.0f)}));
  REQUIRE(restored.element == Element::WATER);
}

TEST_CASE("an orb is a round ball on the aim line, bigger when enlarged",
          "[spell]") {
  auto ball = [](std::vector<PlacedGlyph> signs) {
    Simulation sim;
    sim.CastSpell(SpellQuant::Canonical(Make("sand", signs)), {100, 90},
                  {1, 0});
    Step(sim, 1);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      minX = std::min(minX, p.pos.x), maxX = std::max(maxX, p.pos.x);
      minY = std::min(minY, p.pos.y), maxY = std::max(maxY, p.pos.y);
    });
    // Centred on the line it's aimed along, just ahead of the caster
    REQUIRE(std::abs((minY + maxY) * 0.5f - 90.0f) <= 1.0f);
    REQUIRE(minX > 100.0f);
    REQUIRE(std::abs((maxX - minX) - (maxY - minY)) <= 2.0f); // round
    return maxY - minY;
  };
  float plain = ball({Sign("orb", 1.0f)});
  float big = ball({Sign("orb", 1.0f), Sign("expansion", 1.5f)});
  float small = ball({Sign("orb", 1.0f), Sign("expansion", 1.0f, true)});
  REQUIRE(big > plain * 1.4f);
  REQUIRE(small < plain);
}

TEST_CASE("a dragon is cut short without enough water, whole with "
          "collection",
          "[spell]") {
  auto cast = [](Simulation &sim, const Spell &spell) {
    SpellStats stats = SpellQuant::Canonical(spell);
    sim.CastSpell(stats, {100, GRID_H - 10.0f}, {0, -1});
    SpellEffect effect = sim.GetActiveSpellEffects().back();
    for (int i = 0; i < 300 && !sim.GetActiveSpellEffects().empty(); ++i) {
      effect = sim.GetActiveSpellEffects().back();
      sim.Update(DT);
    }
    return effect; // its last state before it finished
  };
  Spell dragon = Make("water");
  dragon.glyphs.push_back({"dragon", GlyphKind::Sigil, {0, 80}, 1.0f, 0.0f});
  int need = SpellShapes::MaterialNeeded(
      SpellShapes::Get(SpellShape::Dragon),
      SpellQuant::Canonical(dragon).diameter);
  REQUIRE(SpellQuant::Canonical(dragon).particleCount < need);

  // The sigil alone: out of water before the tail
  Simulation dry;
  Floor(dry);
  SpellEffect cut = cast(dry, dragon);
  REQUIRE(cut.shapePart < 2);

  // With water around and collection signs, the whole figure
  Simulation wet;
  Floor(wet);
  Fill(wet, 70, GRID_H - 9, 130, GRID_H - 5, Element::WATER);
  dragon.glyphs.push_back(Sign("collection", 1.5f));
  SpellEffect whole = cast(wet, dragon);
  REQUIRE(whole.shapePart == 2); // playing the tail when it finished
  REQUIRE(whole.emitted <= need);
}

TEST_CASE("earth hardened enough is rock, and rock lands as rock",
          "[spell]") {
  REQUIRE(SpellQuant::Canonical(Make("earth", {Sign("strengthening", 0.5f)}))
              .element == Element::EARTH);
  REQUIRE(SpellQuant::Canonical(Make("earth", {Sign("strengthening", 1.0f)}))
              .element == Element::ROCK);
  REQUIRE(SpellQuant::Canonical(Make("earth", {Sign("convergence", 2.0f)}))
              .element == Element::ROCK);

  Simulation sim;
  sim.SetSeed(2);
  Floor(sim);
  sim.CastSpell(SpellQuant::Canonical(Make("rock")), {60, GRID_H - 20.0f},
                {1, 0.3f});
  Step(sim, 300);
  int live = 0;
  sim.GetParticleSystem().ForEachActive([&](Particle &) { live++; });
  REQUIRE(live == 0); // settled, not crumbling over and over
  REQUIRE(Count(sim, Element::ROCK) > 30);
}

TEST_CASE("fire melts ice", "[spell]") {
  Simulation sim;
  sim.SetSeed(2);
  Floor(sim);
  Fill(sim, 100, GRID_H - 14, 109, GRID_H - 5, Element::ICE);
  Fill(sim, 96, GRID_H - 14, 99, GRID_H - 5, Element::FIRE);
  Fill(sim, 110, GRID_H - 14, 113, GRID_H - 5, Element::FIRE);
  Step(sim, 300);
  REQUIRE(Count(sim, Element::ICE) < 30);
  REQUIRE(Count(sim, Element::WATER) > 50);
  REQUIRE(Count(sim, Element::WATER) < 130); // melted, not multiplied
}

TEST_CASE("collection draws nearby water into the spell", "[spell]") {
  Simulation sim;
  sim.SetSeed(9);
  Floor(sim);
  Fill(sim, 90, GRID_H - 8, 110, GRID_H - 5, Element::WATER);
  Step(sim, 1);
  int water = Count(sim, Element::WATER);

  SpellStats stats =
      SpellQuant::Canonical(Make("water", {Sign("collection", 1.0f)}));
  REQUIRE(stats.collectMax > 0);
  sim.CastSpell(stats, {100, GRID_H - 10.0f}, {0, -1});
  REQUIRE(sim.GetActiveSpellEffects().size() == 1);
  int bonus = sim.GetActiveSpellEffects()[0].bonusParticles;
  REQUIRE(bonus > 0);
  REQUIRE(bonus <= stats.collectMax);
  REQUIRE(Count(sim, Element::WATER) == water - bonus);
}

TEST_CASE("an orb leaves all at once, a stream over several ticks",
          "[spell]") {
  Simulation sim;
  SpellStats orb = SpellQuant::Canonical(Make("sand", {Sign("orb", 1.0f)}));
  REQUIRE(orb.shape == SpellShape::Orb);
  sim.CastSpell(orb, {100, 60}, {1, 0});
  Step(sim, 1);
  REQUIRE(sim.GetActiveSpellEffects().empty());

  SpellStats stream = SpellQuant::Canonical(Make("sand"));
  sim.CastSpell(stream, {100, 60}, {1, 0});
  Step(sim, 1);
  REQUIRE(sim.GetActiveSpellEffects().size() == 1);
}

TEST_CASE("the dragon sigil shapes a spell but needs an element", "[spell]") {
  Spell dragon = Make("fire");
  dragon.glyphs.push_back({"dragon", GlyphKind::Sigil, {0, 80}, 1.0f, 0.0f});
  SpellStats s = SpellSystem::Evaluate(dragon);
  REQUIRE(s.valid);
  REQUIRE(s.shape == SpellShape::Dragon);

  Spell alone;
  alone.glyphs = {{"dragon", GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f},
                  {"levitation", GlyphKind::Sign, {0, -120}, 1.0f, 0.0f}};
  REQUIRE_FALSE(SpellSystem::Evaluate(alone).valid);
}

TEST_CASE("a layered spell fires every part and takes longer to cast",
          "[spell]") {
  Spell water = Make("water"), fire = Make("fire"), wind = Make("wind_underfoot");
  Spell layered;
  layered.name = "layered";
  layered.components = {Part(water, 0.4f), Part(fire, 0.4f, 45.0f),
                        Part(wind, 0.2f)};
  layered.glyphs.push_back(Sign("cooling", 0.5f));

  SpellStats s = SpellQuant::Canonical(layered);
  REQUIRE(s.valid);
  REQUIRE(s.kind == SpellKind::Compound);
  REQUIRE(s.parts.size() == 3);
  REQUIRE(s.HasFlight());
  // The outer ring's cooling reaches every part
  REQUIRE(s.parts[0].temperatureDelta < 0.0f);
  REQUIRE(s.parts[1].temperature <
          SpellQuant::Canonical(fire).temperature);
  // Scale sets how much each part fires
  REQUIRE(s.parts[2].launchSpeed <
          SpellQuant::Canonical(wind).launchSpeed);

  int water_t = TurnController::CastTicks(s.parts[0]);
  int fire_t = TurnController::CastTicks(s.parts[1]);
  int wind_t = TurnController::CastTicks(s.parts[2]);
  int longest = std::max({water_t, fire_t, wind_t});
  int ticks = TurnController::CastTicks(s);
  REQUIRE(ticks > longest);
  REQUIRE(ticks < water_t + fire_t + wind_t + 12);
  REQUIRE(ticks ==
          longest + (water_t + fire_t + wind_t - longest + 1) / 2 + 8 + 4);

  Simulation sim;
  sim.CastSpell(s, {100, 60}, {1, 0});
  // Flight moves the caster; the other two are effects
  REQUIRE(sim.GetActiveSpellEffects().size() == 2);

  // Past five parts it's a chaos-room spell, past the ceiling no spell;
  // and nothing but signs in the outer ring
  layered.components.assign(LAYER_HARD_MAX_COMPONENTS + 1, Part(water, 0.2f));
  REQUIRE_FALSE(SpellSystem::Evaluate(layered).valid);
  layered.components.resize(6);
  REQUIRE(SpellSystem::Evaluate(layered).valid);
  layered.components.resize(5);
  REQUIRE(SpellSystem::Evaluate(layered).valid);
  layered.glyphs.push_back({"fire", GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f});
  REQUIRE_FALSE(SpellSystem::Evaluate(layered).valid);
}

TEST_CASE("quantized layered stats survive a JSON round trip", "[spell]") {
  Spell layered;
  layered.components = {Part(Make("earth", {Sign("crushing", 1.0f, true)}),
                             0.3f),
                        Part(Make("water", {Sign("orb", 1.0f)}), 0.5f)};
  SpellQuant::Stats q =
      SpellQuant::Quantize(SpellSystem::Evaluate(layered));
  nlohmann::json j = q;
  REQUIRE(j.at("parts").size() == 2);
  REQUIRE(j.get<SpellQuant::Stats>() == q);
  REQUIRE(SpellQuant::Quantize(SpellQuant::Dequantize(q)) == q);
}

TEST_CASE("snapshots keep spell modifiers in flight", "[snapshot]") {
  Simulation a;
  a.SetSeed(11);
  Floor(a);
  Spell spell = Make("water", {Sign("cooling", 0.5f), Sign("crushing", 1.0f),
                               Sign("strengthening", 1.0f)});
  spell.glyphs.push_back({"dragon", GlyphKind::Sigil, {0, 80}, 1.0f, 0.0f});
  a.CastSpell(SpellQuant::Canonical(spell), {100, GRID_H - 20.0f}, {1, 0});
  Step(a, 5);

  auto snap = a.SaveSnapshot();
  Simulation b;
  REQUIRE(b.LoadSnapshot(snap));
  REQUIRE(b.SaveSnapshot() == snap);
  Step(a, 30);
  Step(b, 30);
  REQUIRE(a.SaveSnapshot() == b.SaveSnapshot());
}

TEST_CASE("the new glyphs load, with expansion's inverted drawing",
          "[spell]") {
  SvgLibrary library;
  library.LoadFromDirectories(WHAS_SOURCE_DIR "/assets/signs",
                              WHAS_SOURCE_DIR "/assets/sigils");
  for (const char *id : {"convergence", "crushing", "repetition", "cooling",
                         "strengthening", "collection", "expansion", "orb"}) {
    const SvgAsset *a = library.FindById(id);
    INFO(id);
    REQUIRE(a);
    REQUIRE(a->kind == GlyphKind::Sign);
    REQUIRE(a->invertedSegments.size() > 0);
  }
  REQUIRE(library.FindById("dragon"));
  REQUIRE(library.FindById("dragon")->kind == GlyphKind::Sigil);
  // Its own drawing, not a separate glyph
  REQUIRE_FALSE(library.FindById("expansion.inverted"));
  const SvgAsset *expansion = library.FindById("expansion");
  REQUIRE(expansion->invertedSegments.size() !=
          expansion->segments.size());
}

TEST_CASE("layered spells and inverted signs survive the spell file",
          "[spell]") {
  Spell layered;
  layered.components = {
      Part(Make("earth", {Sign("crushing", 1.0f, true)}), 0.3f, 45.0f)};
  layered.glyphs.push_back(Sign("expansion", 0.8f, true));
  nlohmann::json j;
  SpellJson::Write(j, layered);
  Spell back;
  SpellJson::Read(j, back);
  REQUIRE(back.components.size() == 1);
  REQUIRE(back.components[0].rotationDeg == 45.0f);
  REQUIRE(back.components[0].glyphs.back().inverted);
  REQUIRE(back.glyphs[0].inverted);
  REQUIRE(SpellQuant::Canonical(back).parts.size() == 1);
  // Plain spells keep their old form
  nlohmann::json plain;
  SpellJson::Write(plain, Make("water"));
  REQUIRE_FALSE(plain.contains("components"));
  REQUIRE_FALSE(plain["glyphs"][0].contains("inverted"));
}


TEST_CASE("every spell shape is defined and can be picked", "[spell]") {
  for (size_t i = 0; i < static_cast<size_t>(SpellShape::Count); ++i) {
    SpellShape shape = static_cast<SpellShape>(i);
    const ShapeDef &def = SpellShapes::Get(shape);
    INFO(def.name);
    REQUIRE(def.shape == shape);
    REQUIRE_FALSE(def.parts.empty());
    for (const ShapePart &part : def.parts) {
      bool hasPattern = part.beamLanes || part.disk;
      for (int r = 0; r < SpellShapes::ScaledRows(part, 1.0f); ++r)
        hasPattern |= !SpellShapes::RowOffsets(part, r, 1.0f).empty();
      REQUIRE(hasPattern);
    }
    if (shape != SpellShape::Stream)
      REQUIRE(std::any_of(SpellShapes::Triggers().begin(),
                          SpellShapes::Triggers().end(),
                          [shape](auto &t) { return t.shape == shape; }));
  }
  // Streams and orbs use what they have; the dragon is a set figure that
  // needs more material the bigger it's drawn
  REQUIRE(SpellShapes::MaterialNeeded(SpellShapes::Get(SpellShape::Stream),
                                      6.0f) == -1);
  REQUIRE(SpellShapes::MaterialNeeded(SpellShapes::Get(SpellShape::Orb),
                                      6.0f) == -1);
  int dragon =
      SpellShapes::MaterialNeeded(SpellShapes::Get(SpellShape::Dragon), 6.0f);
  REQUIRE(dragon > 100);
  REQUIRE(SpellShapes::MaterialNeeded(SpellShapes::Get(SpellShape::Dragon),
                                      9.0f) > dragon * 2);

  // Art is centred on the aim line, and resampled when drawn bigger
  const ShapePart &head = SpellShapes::Get(SpellShape::Dragon).parts[0];
  REQUIRE(SpellShapes::RowOffsets(head, 0, 1.0f) ==
          std::vector<float>{-0.5f, 0.5f});
  REQUIRE(SpellShapes::ScaledRows(head, 2.0f) == 24);
  REQUIRE(SpellShapes::RowOffsets(head, 0, 2.0f) ==
          std::vector<float>{-1.25f, -0.75f, 0.75f, 1.25f});
}




TEST_CASE("small circles in glyph drawings survive loading", "[glyph]") {
  SvgLibrary library;
  library.LoadFromDirectories(WHAS_SOURCE_DIR "/assets/signs",
                              WHAS_SOURCE_DIR "/assets/sigils");
  // The human's head: a circle of radius 9 at (50, 20) in a 100-wide
  // drawing, loaded at 64 wide
  const SvgAsset *human = library.FindById("human");
  REQUIRE(human);
  Vector2 head{50 * 0.64f, 20 * 0.64f};
  float r = 9 * 0.64f;
  float around = 0.0f;
  for (const LineSeg &seg : human->segments) {
    float da = std::hypot(seg.a.x - head.x, seg.a.y - head.y);
    float db = std::hypot(seg.b.x - head.x, seg.b.y - head.y);
    if (std::abs(da - r) < 0.5f && std::abs(db - r) < 0.5f)
      around += std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
  }
  REQUIRE(around > 2 * PI * r * 0.9f); // the whole way round
}

namespace {

// Where the flying particles are: distinct cells, and their spread (RMS
// distance from their centre)
struct Figure {
  int cells = 0;
  float spread = 0.0f;
  Vector2 centre{0, 0};
};

Figure Measure(Simulation &sim) {
  std::vector<Vector2> at;
  sim.GetParticleSystem().ForEachActive([&](Particle &p) {
    if (p.isProjectile)
      at.push_back(p.pos);
  });
  Figure f;
  if (at.empty())
    return f;
  std::set<std::pair<int, int>> cells;
  for (Vector2 p : at) {
    cells.insert({(int)std::floor(p.x), (int)std::floor(p.y)});
    f.centre.x += p.x / at.size();
    f.centre.y += p.y / at.size();
  }
  float sum = 0.0f;
  for (Vector2 p : at)
    sum += (p.x - f.centre.x) * (p.x - f.centre.x) +
           (p.y - f.centre.y) * (p.y - f.centre.y);
  f.cells = (int)cells.size();
  f.spread = std::sqrt(sum / at.size());
  return f;
}

} // namespace

TEST_CASE("a steered orb turns as a ball", "[spell]") {
  Simulation sim;
  sim.SetSeed(23);
  Spell orb = Make("water", {Sign("orb", 1.0f), Sign("sights_set", 3.0f)});
  sim.CastSpell(SpellQuant::Canonical(orb), {60, 120}, {1, 0}, 1);
  auto tick = [&](int n) {
    for (int i = 0; i < n; ++i) {
      sim.GetParticleSystem().SetCursors({{1, {80, 20}}}); // up and ahead
      sim.Update(DT);
    }
  };
  tick(2);
  Figure start = Measure(sim);
  REQUIRE(start.cells > 10);
  tick(30);
  Figure turned = Measure(sim);
  REQUIRE(turned.centre.y < start.centre.y - 5.0f); // it did turn
  // Still the same ball: not smeared out, not balled up
  REQUIRE(turned.spread < start.spread * 1.3f);
  REQUIRE(turned.spread > start.spread * 0.7f);
}

TEST_CASE("a guided dragon keeps its body while it curves", "[spell]") {
  Simulation sim;
  sim.SetSeed(29);
  Spell dragon = Make("water", {Sign("collection", 1.0f),
                                {"guidance", GlyphKind::Sigil, {0, 120}, 1.5f, 0},
                                {"human", GlyphKind::Sigil, {-100, 60}, 0.5f, 0}});
  dragon.glyphs.push_back({"dragon", GlyphKind::Sigil, {100, 60}, 1.0f, 0.0f});
  SpellStats stats = SpellQuant::Canonical(dragon);
  REQUIRE(stats.valid);
  REQUIRE(stats.shape == SpellShape::Dragon);
  // The target above the line of fire, within guidance's reach
  Character target;
  target.id = 2;
  target.pos = {100, 100};
  sim.GetParticleSystem().SetHurtboxes({{2, target.Bounds()}});
  sim.CastSpell(stats, {40, 150}, {1, 0}, 1);
  // Until it reaches the target the whole body flies on: as many cells
  // and as spread out as once it had formed, however much it curves
  Figure formed;
  bool hit = false;
  for (int i = 0; i < 120 && !hit; ++i) {
    sim.Update(DT);
    hit = !sim.GetParticleSystem().TakeHits().empty();
    Figure f = Measure(sim);
    if (i == 12)
      formed = f;
    if (i > 12 && !hit) {
      REQUIRE(f.cells >= formed.cells * 0.9f);
      REQUIRE(f.spread >= formed.spread * 0.9f);
    }
  }
  REQUIRE(hit); // it curved up onto the target
}

namespace {

int Flying(Simulation &sim, Element e) {
  int n = 0;
  sim.GetParticleSystem().ForEachActive([&](Particle &p) {
    n += p.element == e && p.isProjectile && p.remainingDistance > 0.0f;
  });
  return n;
}

int Loose(Simulation &sim, Element e) {
  int n = 0;
  sim.GetParticleSystem().ForEachActive(
      [&](Particle &p) { n += p.element == e && !p.isProjectile; });
  return n;
}

} // namespace

TEST_CASE("water meets fire in the air: the fire goes out, the water flies on",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(31);
  sim.CastSpell(SpellQuant::Canonical(Make("water")), {80, 60}, {1, 0}, 1);
  sim.CastSpell(SpellQuant::Canonical(Make("fire")), {180, 60}, {-1, 0}, 2);
  // Each flies ~1.2 cells a tick: they meet halfway after ~45 ticks. Look
  // just after the fire is gone, while the water is still in range.
  int steam = 0;
  for (int i = 0; i < 80 && (steam <= 5 || Flying(sim, Element::FIRE) > 0);
       ++i) {
    sim.Update(DT);
    steam = std::max(steam, Count(sim, Element::STEAM) +
                                Loose(sim, Element::STEAM));
  }
  REQUIRE(steam > 5);
  REQUIRE(Flying(sim, Element::FIRE) == 0);
  REQUIRE(Flying(sim, Element::WATER) > 5); // still on its way
}

TEST_CASE("earth ploughs through water and splashes it aside", "[spell]") {
  Simulation sim;
  sim.SetSeed(37);
  Floor(sim);
  // A column of water across the line of fire (the bolt arrives before it
  // has spread far)
  Fill(sim, 122, GRID_H - 30, 138, GRID_H - 5, Element::WATER);
  int pool = Count(sim, Element::WATER);
  // Close enough that the pool is well within the bolt's range
  sim.CastSpell(SpellQuant::Canonical(Make("earth")), {100, GRID_H - 20.0f},
                {1, 0.0f}, 1);
  float before = 0.0f;
  bool splashed = false, through = false;
  for (int i = 0; i < 90; ++i) {
    sim.Update(DT);
    splashed = splashed || Loose(sim, Element::WATER) > 3;
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      if (p.element != Element::EARTH || !p.isProjectile)
        return;
      float speed = std::hypot(p.vel.x, p.vel.y);
      if (p.pos.x < 118)
        before = std::max(before, speed);
      if (p.pos.x > 142 && speed < before && speed > before * 0.4f)
        through = true; // past the pool, slower but still flying
    });
  }
  REQUIRE(splashed);
  REQUIRE(through);
  REQUIRE(Count(sim, Element::WATER) < pool);
}

TEST_CASE("a water bolt puts out the fire it flies through", "[spell]") {
  Simulation sim;
  sim.SetSeed(41);
  Fill(sim, 120, 50, 140, 70, Element::FIRE);
  // Fire along the bolt's lane (the rest keeps burning and spreading)
  auto lane = [&] {
    int n = 0;
    for (int y = 58; y <= 62; ++y)
      for (int x = 120; x <= 140; ++x)
        n += sim.GetCell(x, y).element == Element::FIRE;
    return n;
  };
  int before = lane();
  sim.CastSpell(SpellQuant::Canonical(Make("water")), {105, 60}, {1, 0}, 1);
  Step(sim, 35); // long enough to cross it
  REQUIRE(lane() < before / 3);
  REQUIRE(Count(sim, Element::STEAM) > 10); // it went up in steam
}

TEST_CASE("a figure formed in the ground leaves out what overlaps it",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(43);
  Floor(sim);
  // Aimed along the floor from just above it: the lower half of the ball
  // would form inside the ground
  Spell orb = Make("sand", {Sign("orb", 1.0f), Sign("expansion", 1.5f)});
  SpellStats stats = SpellQuant::Canonical(orb);
  sim.CastSpell(stats, {100, GRID_H - 5.0f}, {1, 0}, 1);
  Step(sim, 1);
  int inGround = 0, flying = 0;
  sim.GetParticleSystem().ForEachActive([&](Particle &p) {
    if (!p.isProjectile)
      return;
    flying++;
    inGround += p.pos.y >= GRID_H - 4;
  });
  REQUIRE(flying > 0);
  REQUIRE(flying < stats.particleCount);
  REQUIRE(inGround == 0);
}

namespace {

// Fire and burning cells
int Burning(const Simulation &sim) {
  int n = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &c = sim.GetCell(x, y);
      n += c.element == Element::FIRE || (c.flags & CELL_BURNING);
    }
  return n;
}

Spell GuidedAt(const char *sigil, const char *target) {
  return Make(sigil, {{"guidance", GlyphKind::Sigil, {0, 120}, 1.0f, 0},
                      {target, GlyphKind::Sigil, {-100, 60}, 0.5f, 0}});
}

} // namespace

TEST_CASE("guided water puts out the fire it was sent after", "[spell]") {
  // A burning pile off to the side of where the water is aimed. How much is
  // burning each tick, with and without the water.
  auto run = [](bool cast) {
    Simulation sim;
    sim.SetSeed(47);
    Floor(sim);
    Fill(sim, 150, GRID_H - 14, 160, GRID_H - 5, Element::WOOD);
    Fill(sim, 150, GRID_H - 24, 160, GRID_H - 15, Element::FIRE);
    if (cast)
      sim.CastSpell(SpellQuant::Canonical(GuidedAt("water", "fire")),
                    {110, GRID_H - 40.0f}, {1, -0.5f}, 1);
    std::vector<int> burning;
    for (int i = 0; i < 90; ++i) {
      Step(sim, 1);
      burning.push_back(Burning(sim));
    }
    return burning;
  };
  // The wood it didn't soak flares up again afterwards, so look at the
  // moment it lands: a good share of the fire goes out
  std::vector<int> left = run(false), doused = run(true);
  bool putOut = false;
  for (size_t i = 0; i < left.size(); ++i)
    putOut |= doused[i] < left[i] * 3 / 4;
  REQUIRE(putOut);
}

TEST_CASE("guided water goes after an enemy's fireball first", "[spell]") {
  // The enemy's fireball crosses high up; a fire burns on the ground nearer
  // the water. Aimed straight ahead, the water turns to meet the fireball.
  auto fireballLeft = [](const Spell &water) {
    Simulation sim;
    sim.SetSeed(53);
    Floor(sim);
    Fill(sim, 120, GRID_H - 10, 124, GRID_H - 5, Element::FIRE);
    sim.CastSpell(SpellQuant::Canonical(Make("fire")), {180, 60}, {-1, 0}, 2);
    sim.CastSpell(SpellQuant::Canonical(water), {100, 80}, {1, 0}, 1);
    // Looked at before the fireball's own range runs out (~48 ticks)
    int flying = 0;
    for (int i = 0; i < 44; ++i) {
      sim.Update(DT);
      flying = 0;
      sim.GetParticleSystem().ForEachActive([&](Particle &p) {
        flying += p.element == Element::FIRE && p.isProjectile && p.owner == 2;
      });
    }
    return flying;
  };
  int unguided = fireballLeft(Make("water"));
  int guided = fireballLeft(GuidedAt("water", "fire"));
  REQUIRE(unguided > 10);
  REQUIRE(guided < unguided / 2);
}


TEST_CASE("a small water spell is taken into a big water ball", "[spell]") {
  Simulation sim;
  sim.SetSeed(59);
  // A big ball flying right, and a thin stream of the enemy's crossing it
  Spell ball = Make("water", {Sign("orb", 1.0f), Sign("expansion", 1.5f)});
  Spell thin = Make("water");
  thin.glyphs[0].scale = 0.1f;
  sim.CastSpell(SpellQuant::Canonical(ball), {60, 90}, {1, 0}, 1);
  sim.CastSpell(SpellQuant::Canonical(thin), {110, 40}, {0, 1}, 2);
  auto flying = [&](int owner) {
    int n = 0;
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      n += p.element == Element::WATER && p.isProjectile && p.owner == owner;
    });
    return n;
  };
  Step(sim, 2);
  int before = flying(1);
  int enemy = SpellQuant::Canonical(thin).particleCount;
  REQUIRE(before > enemy * 3);
  Step(sim, 45);
  // The ball is whole (drops it took in fly with it), not chipped away
  REQUIRE(flying(1) >= before);
}

TEST_CASE("convergence makes spells faster, tighter and smaller", "[spell]") {
  SpellStats plain = SpellSystem::Evaluate(Make("fire"));
  SpellStats tight = SpellSystem::Evaluate(Make("fire", {Sign("convergence", 1.0f)}));
  REQUIRE(tight.valid);
  REQUIRE(tight.speed > plain.speed);
  REQUIRE(tight.range > plain.range);
  REQUIRE(tight.density > plain.density);
  REQUIRE(tight.diameter < plain.diameter);
  REQUIRE(tight.particleCount < plain.particleCount);

  // Wind underfoot throws harder, so further and higher
  SpellStats hop = SpellSystem::Evaluate(Make("wind_underfoot"));
  SpellStats jet = SpellSystem::Evaluate(
      Make("wind_underfoot", {Sign("convergence", 1.0f)}));
  REQUIRE(jet.valid);
  REQUIRE(jet.launchSpeed > hop.launchSpeed);

  // A field becomes a narrower, stronger jet
  SpellStats gust = SpellSystem::Evaluate(Make("wind", {Sign("pulling", 1.0f)}));
  SpellStats blast = SpellSystem::Evaluate(
      Make("wind", {Sign("pulling", 1.0f), Sign("convergence", 1.0f)}));
  REQUIRE(blast.valid);
  REQUIRE(blast.force > gust.force);
  REQUIRE(blast.diameter < gust.diameter);
}

namespace {

int Held(const Simulation &sim, Element e) {
  int n = 0;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &c = sim.GetCell(x, y);
      n += c.element == e && (c.flags & CELL_HELD);
    }
  return n;
}

// Ticks for a standing column to rise its full length, and a couple more
int RiseTicks(const SpellStats &s) {
  return static_cast<int>(std::ceil(s.holdLength / s.holdRise / DT)) + 2;
}

Spell ColumnOf(const char *sigil, bool levitate, bool repetition = false) {
  Spell s;
  s.name = "col";
  s.glyphs = {{sigil, GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f},
              {"column", GlyphKind::Sign, {100, 0}, 1.0f, 0.0f}};
  if (levitate)
    s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 2.0f, 0.0f});
  if (repetition)
    s.glyphs.push_back({"repetition", GlyphKind::Sign, {-100, 0}, 1.0f, 0.0f});
  return s;
}

} // namespace

TEST_CASE("a standing column is done once built: earth stays, water falls",
          "[spell]") {
  for (const char *sigil : {"earth", "water"}) {
    Simulation sim;
    sim.SetSeed(11);
    Floor(sim);
    SpellStats stats = SpellQuant::Canonical(ColumnOf(sigil, false));
    REQUIRE(stats.valid);
    REQUIRE(stats.speed == 0.0f);
    Element e = stats.element;
    int floor = Count(sim, e);
    // Aimed straight up from just above the floor
    sim.CastSpell(stats, {100, GRID_H - 12.0f}, {0, -1});
    Step(sim, RiseTicks(stats));
    REQUIRE(Held(sim, e) == 0);
    REQUIRE(sim.GetActiveSpellEffects().empty());
    int built = Count(sim, e) - floor;
    // A wall raises well more than the spell would throw
    REQUIRE(built >= stats.particleCount * 2);
    Step(sim, 60);
    bool standing = true;
    for (int y = GRID_H - 25; y < GRID_H - 20; ++y)
      standing &= sim.GetCell(100, y).element == e;
    REQUIRE(standing == (e == Element::EARTH));
  }
}

TEST_CASE("repetition mends a launched block, without it damage stays",
          "[spell]") {
  for (bool repetition : {false, true}) {
    Simulation sim;
    sim.SetSeed(12);
    Floor(sim);
    SpellStats stats =
        SpellQuant::Canonical(ColumnOf("earth", true, repetition));
    sim.CastSpell(stats, {60, GRID_H - 40.0f}, {1, 0});
    Step(sim, (int)(stats.range / stats.speed * 60) + 4);
    int held = Held(sim, Element::EARTH);
    REQUIRE(held > 10);
    for (int y = 0; y < GRID_H; ++y)
      for (int x = 0; x < GRID_W; ++x)
        if (sim.GetCell(x, y).flags & CELL_HELD) {
          sim.Erase(x, y, 1); // a hole knocked in it
          y = GRID_H;
          break;
        }
    Step(sim, 10);
    if (repetition)
      REQUIRE(Held(sim, Element::EARTH) == held);
    else
      REQUIRE(Held(sim, Element::EARTH) < held);
  }
}

TEST_CASE("levitation launches a column as one block that lands held",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(13);
  Floor(sim);
  SpellStats stats = SpellQuant::Canonical(ColumnOf("sand", true));
  REQUIRE(stats.speed > 0.0f);
  sim.CastSpell(stats, {60, GRID_H - 40.0f}, {1, 0});
  Step(sim, 2);
  REQUIRE(Held(sim, Element::SAND) == 0); // flying
  Step(sim, (int)(stats.range / stats.speed * 60) + 4);
  int held = Held(sim, Element::SAND);
  REQUIRE(held > stats.particleCount / 2);
  // Set down near where it flew, still in the air: held, not fallen
  int far = 0;
  for (int y = GRID_H - 60; y < GRID_H - 20; ++y)
    for (int x = 60 + (int)stats.range - 10; x < GRID_W; ++x)
      far += sim.GetCell(x, y).element == Element::SAND;
  REQUIRE(far > held / 2);
}

TEST_CASE("a column rises out of its base, quicker with more column signs",
          "[spell]") {
  SpellStats one = SpellQuant::Canonical(ColumnOf("earth", false));
  Spell twoSigns = ColumnOf("earth", false);
  twoSigns.glyphs.push_back({"column", GlyphKind::Sign, {-100, 0}, 1.0f, 0.0f});
  SpellStats two = SpellQuant::Canonical(twoSigns);
  Spell big = ColumnOf("earth", false);
  big.glyphs.push_back(Sign("expansion", 2.0f));
  SpellStats bigger = SpellQuant::Canonical(big);
  REQUIRE(one.holdRise > 0.0f);
  REQUIRE(two.holdRise > one.holdRise);
  REQUIRE(bigger.holdLength * bigger.holdWidth > one.holdLength * one.holdWidth);
  REQUIRE(bigger.holdRise < one.holdRise);
  // Levitated, it flies instead
  REQUIRE(SpellQuant::Canonical(ColumnOf("earth", true)).holdRise == 0.0f);

  Simulation sim;
  sim.SetSeed(14);
  Floor(sim);
  int floor = Count(sim, Element::EARTH);
  sim.CastSpell(one, {100, GRID_H - 4.5f}, {0, -1}, -1, true);
  Step(sim, 2);
  int early = Count(sim, Element::EARTH) - floor;
  REQUIRE(early > 0);
  REQUIRE(early < one.particleCount / 2);
  Step(sim, RiseTicks(one));
  REQUIRE(Count(sim, Element::EARTH) - floor > early * 2);
}

TEST_CASE("column signs turn the block like levitation turns a spell",
          "[spell]") {
  Spell straight = ColumnOf("earth", false);
  Spell turned = straight;
  turned.glyphs[1].rotationDeg = 90.0f; // the column sign points right
  SpellStats a = SpellQuant::Canonical(straight);
  SpellStats b = SpellQuant::Canonical(turned);
  REQUIRE(a.offsetRad == 0.0f);
  REQUIRE(b.offsetRad > 0.5f);
  turned.glyphs[1].rotationDeg = -90.0f;
  REQUIRE(SpellQuant::Canonical(turned).offsetRad < -0.5f);
}

TEST_CASE("a column cast at a slant is as solid as an upright one", "[spell]") {
  SpellStats stats = SpellQuant::Canonical(ColumnOf("earth", false));
  auto cast = [&](Simulation &sim, Vector2 dir) {
    sim.SetSeed(17);
    sim.CastSpell(stats, {200, GRID_H / 2.0f}, dir, -1, true);
    for (int i = 0; i < RiseTicks(stats); ++i)
      sim.Update(DT);
  };
  Simulation straight, slanted;
  cast(straight, {0, -1});
  cast(slanted, {0.7071f, -0.7071f});
  int upright = Count(straight, Element::EARTH);
  int slant = Count(slanted, Element::EARTH);
  REQUIRE(slant > upright * 0.9f);
  REQUIRE(slant < upright * 1.1f);
  int holes = 0;
  for (int y = 1; y < GRID_H - 1; ++y)
    for (int x = 1; x < GRID_W - 1; ++x) {
      auto earth = [&](int i, int j) {
        return slanted.GetCell(i, j).element == Element::EARTH;
      };
      holes += !earth(x, y) && earth(x - 1, y) && earth(x + 1, y) &&
               earth(x, y - 1) && earth(x, y + 1);
    }
  REQUIRE(holes == 0);
}

TEST_CASE("column signs set how deep a drill digs, the sigil how much a "
          "column builds",
          "[spell]") {
  auto drillOf = [](float sigil, float column) {
    Spell s = ColumnOf("earth", false);
    s.glyphs[0].scale = sigil;
    s.glyphs[1].scale = column;
    s.glyphs.push_back(Sign("crushing", 1.0f));
    return SpellQuant::Canonical(s);
  };
  SpellStats one = drillOf(1.0f, 1.0f);
  REQUIRE(drillOf(1.0f, 2.0f).holdLength > one.holdLength * 1.5f);
  REQUIRE(drillOf(2.0f, 1.0f).holdLength == one.holdLength);
  REQUIRE(one.holdWidth >= Character::WIDTH + 2);

  Spell small = ColumnOf("earth", false), more = small, big = small;
  more.glyphs[1].scale = 2.0f;
  big.glyphs[0].scale = 2.0f;
  float built = SpellQuant::Canonical(small).holdLength *
                SpellQuant::Canonical(small).holdWidth;
  REQUIRE(SpellQuant::Canonical(more).holdLength *
              SpellQuant::Canonical(more).holdWidth == built);
  REQUIRE(SpellQuant::Canonical(big).holdLength *
              SpellQuant::Canonical(big).holdWidth > built);
}

TEST_CASE("drilling straight down under yourself drops you into the hole",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(18);
  Fill(sim, 0, GRID_H - 40, GRID_W - 1, GRID_H - 1, Element::EARTH);
  Character c;
  c.pos = {196, GRID_H - 40 - Character::HEIGHT};
  for (int i = 0; i < 10; ++i)
    c.Step(sim, {}, DT);
  float before = c.pos.y;
  Spell drill = ColumnOf("earth", false);
  drill.glyphs.push_back(Sign("crushing", 1.0f));
  SpellStats stats = SpellQuant::Canonical(drill);
  INFO("drill " << stats.holdWidth << " x " << stats.holdLength);
  sim.CastSpell(stats, c.Center(), {0, 1}, c.id, false);
  int ticks = RiseTicks(stats) + static_cast<int>(stats.holdTime / DT);
  for (int i = 0; i < ticks; ++i) {
    sim.Update(DT);
    c.Unbury(sim);
    c.Step(sim, {}, DT);
  }
  INFO("fell " << c.pos.y - before);
  REQUIRE(c.pos.y > before + Character::HEIGHT);
}

TEST_CASE("a rising column lifts the character standing over it", "[spell]") {
  Simulation sim;
  sim.SetSeed(15);
  Floor(sim);
  Character c;
  c.pos = {96, GRID_H - 4 - Character::HEIGHT};
  for (int i = 0; i < 10; ++i)
    c.Step(sim, {}, DT);
  float before = c.pos.y;
  SpellStats stats = SpellQuant::Canonical(ColumnOf("earth", false));
  sim.CastSpell(stats, {100, GRID_H - 4.5f}, {0, -1}, -1, true);
  for (int i = 0; i < RiseTicks(stats); ++i) {
    sim.Update(DT);
    c.Step(sim, {}, DT);
  }
  REQUIRE(c.pos.y < before - stats.holdLength * 0.5f);
}

TEST_CASE("a crushing column drills a hole and throws the grit back out",
          "[spell]") {
  Simulation sim;
  sim.SetSeed(16);
  Fill(sim, 0, GRID_H - 4, GRID_W - 1, GRID_H - 1, Element::ROCK);
  Fill(sim, 130, GRID_H - 60, 200, GRID_H - 5, Element::ROCK);
  for (int y = GRID_H - 60; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (sim.GetCell(x, y).element == Element::ROCK)
        sim.Anchor(x, y);
  int rock = Count(sim, Element::ROCK);
  Spell drill = ColumnOf("earth", false);
  drill.glyphs.push_back(Sign("crushing", 1.0f));
  SpellStats stats = SpellQuant::Canonical(drill);
  REQUIRE(stats.holdRise > 0.0f);
  REQUIRE(stats.crush > 0.0f);
  // Placed on the wall's face, it bores in
  sim.CastSpell(stats, {129.5f, GRID_H - 30.0f}, {1, 0}, -1, true);
  int back = 0, ahead = 0;
  for (int i = 0; i < RiseTicks(stats); ++i) {
    sim.Update(DT);
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      if (p.element == Element::SAND)
        (p.vel.x < 0.0f ? back : ahead)++;
    });
  }
  REQUIRE(Count(sim, Element::ROCK) < rock - stats.holdLength);
  REQUIRE(Count(sim, Element::EARTH) == 0);
  REQUIRE(back > ahead * 3);
  // The bore is open along the middle of the drill
  int open = 0;
  for (int x = 131; x < 130 + (int)stats.holdLength - 1; ++x)
    open += sim.GetCell(x, GRID_H - 31).element != Element::ROCK;
  REQUIRE(open >= (int)stats.holdLength - 4);
}

TEST_CASE("crushing debris flies back the way the spell came", "[spell]") {
  Simulation sim;
  sim.SetSeed(17);
  Fill(sim, 150, GRID_H - 60, 175, GRID_H - 5, Element::ROCK);
  for (int y = GRID_H - 60; y <= GRID_H - 5; ++y)
    for (int x = 150; x <= 175; ++x)
      sim.Anchor(x, y);
  SpellStats dig = SpellQuant::Canonical(
      Make("earth", {Sign("crushing", 2.0f), Sign("convergence", 2.0f)}));
  sim.CastSpell(dig, {120, GRID_H - 30.0f}, {1, 0});
  int back = 0, ahead = 0;
  for (int i = 0; i < 60; ++i) {
    sim.Update(DT);
    sim.GetParticleSystem().ForEachActive([&](Particle &p) {
      if (p.element == Element::SAND)
        (p.vel.x < 0.0f ? back : ahead)++;
    });
  }
  REQUIRE(back > 0);
  REQUIRE(back > ahead * 3);
}
