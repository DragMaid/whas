#pragma once
#include "whas/constants.h"
#include "whas/game/map.h"
#include "whas/spell/spell_types.h"
#include <array>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

// A campaign: rooms on a grid, each one screen of terrain with things in
// it. Kept in data/campaigns/<id>/:
//   campaign.json          name, rooms, where it starts, the starting kit
//   rooms/<x>_<y>.json     one room's terrain, background and objects
//   backgrounds/*.png      room backgrounds, scaled to the world
//   backpack/*.json        the player's spells (crafted at workbenches)
//   save.json              progress (see CampaignSave)
namespace Campaign {

struct RoomPos {
  int x = 0;
  int y = 0;
  bool operator==(const RoomPos &) const = default;
  auto operator<=>(const RoomPos &) const = default;
  RoomPos Step(int dx, int dy) const { return {x + dx, y + dy}; }
};

enum class EnemyKind : uint8_t { Mage, Undead, Flyer };
const char *EnemyName(EnemyKind kind);

struct EnemyDef {
  EnemyKind kind = EnemyKind::Undead;
  std::string tag; // for "defeat the tagged ones" conditions
  Vector2 pos{0, 0}; // top-left, cells
  float hp = 40.0f;
  float speed = 20.0f;  // cells/s
  float damage = 8.0f;  // per touch (undead, flyers) or per cast hit scale
  // Mage: spells it picks from at random, copied in so the room doesn't
  // depend on the editor's library
  std::vector<Spell> spells;
  float castEvery = 1.6f; // seconds between a mage's casts

  // A boss: named, with its health across the top of the screen, and
  // phases it moves into as its health drops
  bool boss = false;
  std::string name = "The Brimmed Hat";
  struct Phase {
    float below = 0.5f; // starts when health falls under this share
    float speed = 30.0f;
    float damage = 12.0f;
    float castEvery = 1.0f;
    std::vector<Spell> spells; // a mage's new spells; empty keeps the old
  };
  std::vector<Phase> phases; // highest `below` first

  // Left behind the first time it's beaten: silver and blueprint pages
  int silver = 0;
  std::vector<Spell> drops;
};

// Geysers and rockfalls are hazards: a geyser spouts its element straight
// up every so often; a rockfall hangs from the ceiling and drops a stone on
// whoever walks under it.
enum class ObjectKind : uint8_t { Gate, Workbench, Shrine, Geyser, Rockfall, Count };
const char *ObjectName(ObjectKind kind);

struct ObjectDef {
  ObjectKind kind = ObjectKind::Gate;
  Vector2 pos{0, 0}; // centre on the floor, cells
  // Shrine: the glyph it teaches ("" for none) and whether it's a sigil,
  // and blueprints: ready-made spells it puts in the backpack. A blueprint
  // doesn't teach its glyphs.
  std::string glyph;
  bool sigil = false;
  std::vector<Spell> spells;
  // Buried: hidden in the terrain (placed anywhere, not on the floor) until
  // the player digs it out
  bool hidden = false;
  // Only there once the room's conditions are met (a trial's prize)
  bool reward = false;
  int silver = 0; // Shrine: silver it gives too
  // Hazards: what a geyser spouts (water or fire) and the seconds between
  // spouts, or before a rockfall is ready again
  Element jet = Element::WATER;
  float every = 3.0f;
};
// The cells an object takes up
Rectangle ObjectBox(const ObjectDef &o);

// Talking: node 0 is where a conversation starts. A reply jumps to another
// node, or ends it (next < 0). A node with no replies ends after it's read.
struct DialogueReply {
  std::string text;
  int next = -1;
};
struct DialogueNode {
  std::string text;
  std::vector<DialogueReply> replies; // up to MAX_REPLIES
  std::string teach;                  // glyph id learned on reaching it, or ""
  bool teachSigil = false;
  std::vector<Spell> spells; // blueprints given on reaching it
  int silver = 0;            // given on reaching it (once)
  // A shop: reaching it costs this much silver (once; replies leading here
  // can't be picked without it)
  int price = 0;
};
constexpr int MAX_REPLIES = 3;

// Someone to talk to. NPCs are part of the scenery: spells, enemies and
// guidance pass them by.
// TODO: NPCs reuse the player's sprite; give them their own.
struct NpcDef {
  std::string name = "Stranger";
  std::string tag; // conditions refer to NPCs by tag
  Vector2 pos{0, 0}; // top-left, cells
  std::vector<DialogueNode> dialogue{{"Hello, little witch.", {}, "", false}};
};

// What opens a room's sealed edges. All of a room's conditions must hold.
enum class ConditionKind : uint8_t {
  Defeat,     // every enemy with `tag` (all of them when "") is down
  Break,      // `share` of the solid cells that start in `region` are gone
  Fill,       // `region` holds `amount` cells of `element`
  Talk,       // the NPC with `tag` has been talked to (to `node`, or at all)
  TakeShrine, // the room's shrine has been taken
  Count
};
const char *ConditionName(ConditionKind kind);

struct ConditionDef {
  ConditionKind kind = ConditionKind::Defeat;
  std::string tag;
  Rectangle region{0, 0, 0, 0}; // cells
  float share = 0.9f;
  Element element = Element::ROCK;
  int amount = 50;
  int node = -1;
  std::string hint; // shown to the player; a default is made when ""
};

// A trial: the room's conditions must be met in time and/or with only
// some glyphs. Failing (time running out) starts the room over.
struct TrialDef {
  bool on = false;
  float seconds = 0.0f;         // 0: no time limit
  std::set<std::string> glyphs; // glyphs spells may use; empty: any
};

// Edges a room's conditions can seal
enum Edge : int { EdgeLeft, EdgeRight, EdgeUp, EdgeDown, EDGES };

struct RoomDef {
  RoomPos pos;
  MapDef terrain;         // cells and world settings (spawns unused)
  std::string background; // file in backgrounds/, "" for none
  std::vector<ObjectDef> objects;
  std::vector<EnemyDef> enemies;
  std::vector<NpcDef> npcs;
  std::vector<ConditionDef> conditions;
  std::array<bool, EDGES> sealed{}; // edges closed until the conditions hold
  TrialDef trial;
};

struct CampaignDef {
  static constexpr int FORMAT = 1;
  std::string id; // folder name
  std::string name;
  std::vector<RoomPos> rooms;
  RoomPos startRoom;
  Vector2 startPos{GRID_W * 0.5f, GRID_H * 0.5f}; // top-left, cells
  // Glyph ids a new game starts with
  std::set<std::string> startingKit{"fire", "levitation"};
  // The campaign's own words about glyphs, shown in the codex once known
  std::map<std::string, std::string> lore;

  bool HasRoom(RoomPos p) const;
};

// Progress through one campaign
struct Save {
  std::set<std::string> glyphs; // unlocked glyph ids
  std::set<RoomPos> visited;
  // Activated gates: room and index in its objects
  std::vector<std::pair<RoomPos, int>> gates;
  std::optional<std::pair<RoomPos, int>> respawn; // last gate touched
  std::set<std::pair<RoomPos, int>> shrinesTaken;
  std::array<std::string, 3> slots{}; // backpack spell refs
  std::set<RoomPos> cleared;          // rooms whose conditions were met
  // Conversations reached: "<x>_<y>/<npc tag or index>/<node>"
  std::set<std::string> talked;
  int silver = 0;
  std::set<std::pair<RoomPos, int>> looted; // enemies whose drops were taken
  std::map<std::string, std::string> met;   // "<x>_<y>/<npc>" -> their name
};

std::string TalkKey(RoomPos room, const std::string &npc, int node);

constexpr int SLOTS = 3;
constexpr const char *ROOT = "data/campaigns";

std::string Dir(const std::string &id);
std::string BackpackDir(const std::string &id);
std::string BackgroundPath(const std::string &id, const std::string &file);

nlohmann::json ToJson(const CampaignDef &c);
bool FromJson(const nlohmann::json &j, CampaignDef &c, std::string &error);
nlohmann::json ToJson(const RoomDef &r);
bool FromJson(const nlohmann::json &j, RoomDef &r, std::string &error);
nlohmann::json ToJson(const Save &s);
void FromJson(const nlohmann::json &j, Save &s);

// Every campaign's definition (rooms are loaded one at a time)
std::vector<CampaignDef> LoadAll();
bool SaveDef(const CampaignDef &c, std::string &error);
bool Remove(const std::string &id, std::string &error);
std::string NewId(const std::string &name);

// A blank room: open air with a floor
RoomDef BlankRoom(RoomPos pos);
std::optional<RoomDef> LoadRoom(const std::string &id, RoomPos pos,
                                std::string &error);
bool SaveRoom(const std::string &id, const RoomDef &room, std::string &error);
bool RemoveRoom(const std::string &id, RoomPos pos, std::string &error);

std::optional<Save> LoadSave(const std::string &id);
bool WriteSave(const std::string &id, const Save &save, std::string &error);
void ClearSave(const std::string &id);

// Copy a PNG into the campaign's backgrounds, scaled to the world's size;
// returns the file name it was saved as
std::optional<std::string> ImportBackground(const std::string &id,
                                            const std::string &path,
                                            std::string &error);

} // namespace Campaign
