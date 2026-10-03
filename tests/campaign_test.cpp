#include "whas/campaign/campaign.h"
#include "whas/campaign/enemies.h"
#include "whas/constants.h"
#include "whas/game/character.h"
#include "whas/engine/simulation.h"
#include <catch2/catch_test_macros.hpp>

using namespace Campaign;

namespace {

constexpr float DT = 1.0f / 60.0f;

void Floor(Simulation &sim) {
  for (int y = GRID_H - 8; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      sim.Paint(x, y, Element::ROCK, 0);
  for (int y = GRID_H - 8; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      sim.Anchor(x, y);
}

Spell Bolt() {
  Spell s;
  s.name = "bolt";
  s.glyphs = {{"earth", GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f},
              {"levitation", GlyphKind::Sign, {0, -120}, 1.5f, 0.0f}};
  return s;
}

Enemies::Enemy Make(const Simulation &sim, EnemyKind kind, Vector2 at) {
  RoomDef room = BlankRoom({0, 0});
  EnemyDef def;
  def.kind = kind;
  def.pos = at;
  if (kind == EnemyKind::Mage)
    def.spells = {Bolt()};
  room.enemies = {def};
  std::vector<Enemies::Enemy> out;
  Enemies::Spawn(room, sim, out);
  return out[0];
}

} // namespace

TEST_CASE("rooms and saves survive a round trip", "[campaign]") {
  RoomDef room = BlankRoom({2, -1});
  room.background = "cave.png";
  room.objects = {{ObjectKind::Gate, {40, 200}, "", false},
                  {ObjectKind::Shrine, {80, 200}, "water", true}};
  EnemyDef mage;
  mage.kind = EnemyKind::Mage;
  mage.pos = {100, 150};
  mage.spells = {Bolt()};
  room.enemies = {mage};
  RoomDef back;
  std::string error;
  REQUIRE(FromJson(ToJson(room), back, error));
  REQUIRE(back.pos == room.pos);
  REQUIRE(back.terrain.cells == room.terrain.cells);
  REQUIRE(back.objects.size() == 2);
  REQUIRE(back.objects[1].glyph == "water");
  REQUIRE(back.enemies[0].spells.size() == 1);
  REQUIRE(back.enemies[0].spells[0].glyphs[1].assetId == "levitation");

  Save save;
  save.glyphs = {"fire", "column"};
  save.visited = {{0, 0}, {1, 0}};
  save.gates = {{{1, 0}, 2}};
  save.respawn = {{{1, 0}, 2}};
  save.slots = {"bolt", "", "wall"};
  Save loaded;
  FromJson(ToJson(save), loaded);
  REQUIRE(loaded.glyphs == save.glyphs);
  REQUIRE(loaded.visited == save.visited);
  REQUIRE(loaded.gates == save.gates);
  REQUIRE(loaded.respawn == save.respawn);
  REQUIRE(loaded.slots == save.slots);
}

TEST_CASE("NPCs, conditions and seals survive a round trip", "[campaign]") {
  RoomDef room = BlankRoom({0, 1});
  NpcDef npc;
  npc.name = "Qifrey";
  npc.tag = "teacher";
  npc.pos = {60, 200};
  npc.dialogue = {{"Shall we begin?", {{"Yes", 1}, {"Not yet", -1}}, "", false},
                  {"Then watch closely.", {}, "column", false}};
  room.npcs = {npc};
  ConditionDef fill;
  fill.kind = ConditionKind::Fill;
  fill.region = {100, 180, 30, 20};
  fill.element = Element::WATER;
  fill.amount = 300;
  fill.hint = "Flood the basin";
  ConditionDef talk;
  talk.kind = ConditionKind::Talk;
  talk.tag = "teacher";
  talk.node = 1;
  room.conditions = {fill, talk};
  room.sealed[EdgeRight] = true;
  room.enemies = {EnemyDef{}};
  room.enemies[0].tag = "boss";

  RoomDef back;
  std::string error;
  REQUIRE(FromJson(ToJson(room), back, error));
  REQUIRE(back.npcs.size() == 1);
  REQUIRE(back.npcs[0].dialogue[0].replies[0].next == 1);
  REQUIRE(back.npcs[0].dialogue[1].teach == "column");
  REQUIRE(back.conditions[0].kind == ConditionKind::Fill);
  REQUIRE(back.conditions[0].region.width == 30);
  REQUIRE(back.conditions[0].element == Element::WATER);
  REQUIRE(back.conditions[1].node == 1);
  REQUIRE(back.sealed[EdgeRight]);
  REQUIRE_FALSE(back.sealed[EdgeLeft]);
  REQUIRE(back.enemies[0].tag == "boss");

  Save save;
  save.cleared = {{0, 1}};
  save.talked = {TalkKey({0, 1}, "teacher", 1)};
  Save loaded;
  FromJson(ToJson(save), loaded);
  REQUIRE(loaded.cleared == save.cleared);
  REQUIRE(loaded.talked == save.talked);
}

TEST_CASE("a flyer finds its way around a wall to the player",
          "[campaign]") {
  Simulation sim;
  Floor(sim);
  // A wall from the floor most of the way up, between flyer and player
  for (int y = 40; y < GRID_H - 8; ++y)
    for (int x = 200; x < 206; ++x) {
      sim.Paint(x, y, Element::ROCK, 0);
      sim.Anchor(x, y);
    }
  Character player;
  player.id = 1;
  player.pos = {300, GRID_H - 8 - Character::HEIGHT};
  Enemies::Enemy flyer = Make(sim, EnemyKind::Flyer, {100, GRID_H - 40.0f});
  REQUIRE_FALSE(Enemies::LineOfSight(sim, flyer.body.Center(), player.Center()));

  auto path = Enemies::FindPath(sim, flyer.body.Center(), player.Center());
  REQUIRE_FALSE(path.empty());
  for (Vector2 p : path)
    REQUIRE(Character::Fits(sim, {p.x - Character::WIDTH * 0.5f,
                                  p.y - Character::HEIGHT * 0.5f}));

  std::mt19937 rng(1);
  float startHp = player.hp;
  // Up over the wall and down the far side at 20 cells/s
  for (int i = 0; i < 60 * 40 && player.hp == startHp; ++i)
    Enemies::Tick(flyer, sim, player, rng, DT);
  REQUIRE(player.hp < startHp); // got there and bit
}

TEST_CASE("undead charge and knock the player back", "[campaign]") {
  Simulation sim;
  Floor(sim);
  Character player;
  player.id = 1;
  player.pos = {200, GRID_H - 8 - Character::HEIGHT};
  player.Step(sim, {}, 0.0f);
  Enemies::Enemy undead = Make(sim, EnemyKind::Undead, {150, GRID_H - 30.0f});
  std::mt19937 rng(2);
  bool knocked = false;
  for (int i = 0; i < 60 * 8 && !knocked; ++i) {
    Enemies::Tick(undead, sim, player, rng, DT);
    knocked = player.hp < player.maxHp;
  }
  REQUIRE(knocked);
  REQUIRE(player.pushX > 0.0f); // shoved away from it
}

TEST_CASE("a mage casts its spells at a player it can see", "[campaign]") {
  Simulation sim;
  Floor(sim);
  Character player;
  player.id = 1;
  player.pos = {250, GRID_H - 8 - Character::HEIGHT};
  Enemies::Enemy mage = Make(sim, EnemyKind::Mage, {150, GRID_H - 30.0f});
  REQUIRE(mage.spells.size() == 1);
  std::mt19937 rng(3);
  bool cast = false;
  for (int i = 0; i < 60 * 4 && !cast; ++i) {
    Enemies::Tick(mage, sim, player, rng, DT);
    cast = !sim.GetActiveSpellEffects().empty();
  }
  REQUIRE(cast);
  REQUIRE(sim.GetActiveSpellEffects()[0].owner == mage.body.id);
  REQUIRE(sim.GetActiveSpellEffects()[0].direction.x > 0.5f);
}

// whas_tests "[.sample]" writes a small campaign to play with:
// data/campaigns/sample (two rooms, a gate, a bench, a shrine, enemies)
TEST_CASE("write the sample campaign", "[.sample]") {
  CampaignDef c;
  c.id = "sample";
  c.name = "Sample";
  c.rooms = {{0, 0}, {1, 0}};
  c.startPos = {40, GRID_H - 12 - Character::HEIGHT};
  RoomDef a = BlankRoom({0, 0});
  a.objects = {{ObjectKind::Gate, {70, GRID_H - 12.0f}, "", false},
               {ObjectKind::Workbench, {110, GRID_H - 12.0f}, "", false},
               {ObjectKind::Shrine, {160, GRID_H - 12.0f}, "column", false}};
  EnemyDef undead;
  undead.pos = {300, GRID_H - 40.0f};
  EnemyDef mage;
  mage.kind = EnemyKind::Mage;
  mage.pos = {340, GRID_H - 40.0f};
  mage.spells = {Bolt()};
  EnemyDef flyer;
  flyer.kind = EnemyKind::Flyer;
  flyer.pos = {250, 60};
  a.enemies = {undead, mage, flyer};
  RoomDef b = BlankRoom({1, 0});
  for (int y = 120; y < GRID_H - 12; ++y)
    for (int x = 180; x < 200; ++x)
      b.terrain.cells[static_cast<size_t>(y) * GRID_W + x] =
          static_cast<uint8_t>(Element::EARTH);
  std::string error;
  REQUIRE(SaveDef(c, error));
  REQUIRE(SaveRoom(c.id, a, error));
  REQUIRE(SaveRoom(c.id, b, error));
}
