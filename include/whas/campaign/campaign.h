#pragma once
#include "whas/constants.h"
#include "whas/game/map.h"
#include "whas/spell/spell_types.h"
#include <array>
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
};

enum class ObjectKind : uint8_t { Gate, Workbench, Shrine };
const char *ObjectName(ObjectKind kind);

struct ObjectDef {
  ObjectKind kind = ObjectKind::Gate;
  Vector2 pos{0, 0}; // centre on the floor, cells
  // Shrine: the glyph it teaches and whether it's a sigil
  std::string glyph;
  bool sigil = false;
};

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
