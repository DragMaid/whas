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

struct RoomDef {
  RoomPos pos;
  MapDef terrain;         // cells and world settings (spawns unused)
  std::string background; // file in backgrounds/, "" for none
  std::vector<ObjectDef> objects;
  std::vector<EnemyDef> enemies;
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
};

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
