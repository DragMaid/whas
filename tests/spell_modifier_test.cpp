#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_json.h"
#include "whas/spell/spell_quant.h"
#include "whas/spell/svg_library.h"
#include <algorithm>
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
  s.glyphs.push_back({"column", GlyphKind::Sign, {0, -120}, 2.0f, 0.0f});
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

TEST_CASE("a character buried by a spell pops up on top of the pile",
          "[character]") {
  Simulation sim;
  sim.SetSeed(3);
  Floor(sim);
  Character c;
  c.pos = {50, GRID_H - 4 - Character::HEIGHT};
  // Sand dumped right over the character, up to its head
  Fill(sim, 48, GRID_H - 12, 55, GRID_H - 5, Element::SAND);

  c.Step(sim, {}, DT);
  Rectangle b = c.Bounds();
  for (int y = (int)b.y; y < (int)(b.y + b.height); ++y)
    for (int x = (int)b.x; x < (int)(b.x + b.width); ++x)
      REQUIRE(sim.GetCell(x, y).element == Element::AIR);
  REQUIRE(c.pos.y <= GRID_H - 12 - Character::HEIGHT);
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

TEST_CASE("an orb is a round ball above the caster, bigger when enlarged",
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
    REQUIRE(maxY < 90.0f - 4.0f); // above the caster
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
                  {"column", GlyphKind::Sign, {0, -120}, 1.0f, 0.0f}};
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

  // At most five parts, and nothing but signs in the outer ring
  layered.components.assign(6, Part(water, 0.2f));
  REQUIRE_FALSE(SpellSystem::Evaluate(layered).valid);
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



