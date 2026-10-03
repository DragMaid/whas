#include "whas/campaign/campaign.h"
#include "whas/spell/spell_json.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <raylib.h>

using nlohmann::json;
namespace fs = std::filesystem;

namespace Campaign {

namespace {

json Vec(Vector2 v) { return json::array({v.x, v.y}); }
Vector2 Vec(const json &j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
json Pos(RoomPos p) { return json::array({p.x, p.y}); }
RoomPos Pos(const json &j) { return {j.at(0).get<int>(), j.at(1).get<int>()}; }

std::string RoomFile(const std::string &id, RoomPos p) {
  return Dir(id) + "/rooms/" + std::to_string(p.x) + "_" +
         std::to_string(p.y) + ".json";
}

bool WriteJson(const std::string &path, const json &j, std::string &error) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  // Write beside and rename, so a crash never leaves half a file
  std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp);
    if (!out) {
      error = "can't write " + path;
      return false;
    }
    out << j.dump(1);
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    error = ec.message();
    return false;
  }
  return true;
}

std::optional<json> ReadJson(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    return std::nullopt;
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded())
    return std::nullopt;
  return j;
}

} // namespace

const char *EnemyName(EnemyKind kind) {
  switch (kind) {
  case EnemyKind::Mage:
    return "Mage";
  case EnemyKind::Undead:
    return "Undead";
  case EnemyKind::Flyer:
    return "Flyer";
  }
  return "?";
}

const char *ObjectName(ObjectKind kind) {
  switch (kind) {
  case ObjectKind::Gate:
    return "Windowway gate";
  case ObjectKind::Workbench:
    return "Workbench";
  case ObjectKind::Shrine:
    return "Glyph shrine";
  }
  return "?";
}

const char *ConditionName(ConditionKind kind) {
  switch (kind) {
  case ConditionKind::Defeat:
    return "Defeat enemies";
  case ConditionKind::Break:
    return "Break blocks in a region";
  case ConditionKind::Fill:
    return "Fill a region";
  case ConditionKind::Talk:
    return "Talk to someone";
  case ConditionKind::TakeShrine:
    return "Take the shrine";
  default:
    return "?";
  }
}

std::string TalkKey(RoomPos room, const std::string &npc, int node) {
  return std::to_string(room.x) + "_" + std::to_string(room.y) + "/" + npc +
         "/" + std::to_string(node);
}

bool CampaignDef::HasRoom(RoomPos p) const {
  return std::find(rooms.begin(), rooms.end(), p) != rooms.end();
}

std::string Dir(const std::string &id) { return std::string(ROOT) + "/" + id; }
std::string BackpackDir(const std::string &id) { return Dir(id) + "/backpack"; }
std::string BackgroundPath(const std::string &id, const std::string &file) {
  return Dir(id) + "/backgrounds/" + file;
}

json ToJson(const CampaignDef &c) {
  json rooms = json::array();
  for (RoomPos p : c.rooms)
    rooms.push_back(Pos(p));
  return {{"format", CampaignDef::FORMAT},
          {"name", c.name},
          {"rooms", rooms},
          {"startRoom", Pos(c.startRoom)},
          {"startPos", Vec(c.startPos)},
          {"startingKit", c.startingKit}};
}

bool FromJson(const json &j, CampaignDef &c, std::string &error) {
  try {
    if (j.value("format", 0) != CampaignDef::FORMAT) {
      error = "unknown campaign format";
      return false;
    }
    c.name = j.value("name", "");
    c.rooms.clear();
    for (const json &p : j.at("rooms"))
      c.rooms.push_back(Pos(p));
    c.startRoom = Pos(j.at("startRoom"));
    c.startPos = Vec(j.at("startPos"));
    c.startingKit = j.value("startingKit", std::set<std::string>{});
    return true;
  } catch (const std::exception &e) {
    error = std::string("bad campaign: ") + e.what();
    return false;
  }
}

json ToJson(const RoomDef &r) {
  json objects = json::array();
  for (const ObjectDef &o : r.objects) {
    json oj{{"kind", static_cast<int>(o.kind)}, {"pos", Vec(o.pos)}};
    if (o.kind == ObjectKind::Shrine) {
      oj["glyph"] = o.glyph;
      oj["sigil"] = o.sigil;
    }
    objects.push_back(std::move(oj));
  }
  json enemies = json::array();
  for (const EnemyDef &e : r.enemies) {
    json ej{{"kind", static_cast<int>(e.kind)},
            {"tag", e.tag},
            {"pos", Vec(e.pos)},
            {"hp", e.hp},
            {"speed", e.speed},
            {"damage", e.damage}};
    if (e.kind == EnemyKind::Mage) {
      ej["castEvery"] = e.castEvery;
      ej["spells"] = json::array();
      for (const Spell &s : e.spells) {
        json sj{{"name", s.name}, {"format", SpellJson::FORMAT}};
        SpellJson::Write(sj, s);
        ej["spells"].push_back(std::move(sj));
      }
    }
    enemies.push_back(std::move(ej));
  }
  json npcs = json::array();
  for (const NpcDef &n : r.npcs) {
    json nodes = json::array();
    for (const DialogueNode &d : n.dialogue) {
      json replies = json::array();
      for (const DialogueReply &rep : d.replies)
        replies.push_back({{"text", rep.text}, {"next", rep.next}});
      nodes.push_back({{"text", d.text},
                       {"replies", replies},
                       {"teach", d.teach},
                       {"teachSigil", d.teachSigil}});
    }
    npcs.push_back({{"name", n.name}, {"tag", n.tag}, {"pos", Vec(n.pos)},
                    {"dialogue", nodes}});
  }
  json conditions = json::array();
  for (const ConditionDef &c : r.conditions)
    conditions.push_back(
        {{"kind", static_cast<int>(c.kind)},
         {"tag", c.tag},
         {"region", {c.region.x, c.region.y, c.region.width, c.region.height}},
         {"share", c.share},
         {"element", static_cast<int>(c.element)},
         {"amount", c.amount},
         {"node", c.node},
         {"hint", c.hint}});
  return {{"pos", Pos(r.pos)},
          {"terrain", Maps::ToJson(r.terrain)},
          {"background", r.background},
          {"objects", objects},
          {"enemies", enemies},
          {"npcs", npcs},
          {"conditions", conditions},
          {"sealed", r.sealed}};
}

bool FromJson(const json &j, RoomDef &r, std::string &error) {
  try {
    r.pos = Pos(j.at("pos"));
    if (!Maps::FromJson(j.at("terrain"), r.terrain, error))
      return false;
    r.background = j.value("background", "");
    r.objects.clear();
    for (const json &oj : j.value("objects", json::array())) {
      ObjectDef o;
      o.kind = static_cast<ObjectKind>(std::clamp(oj.value("kind", 0), 0, 2));
      o.pos = Vec(oj.at("pos"));
      o.glyph = oj.value("glyph", "");
      o.sigil = oj.value("sigil", false);
      r.objects.push_back(std::move(o));
    }
    r.enemies.clear();
    for (const json &ej : j.value("enemies", json::array())) {
      EnemyDef e;
      e.kind = static_cast<EnemyKind>(std::clamp(ej.value("kind", 1), 0, 2));
      e.pos = Vec(ej.at("pos"));
      e.hp = ej.value("hp", e.hp);
      e.speed = ej.value("speed", e.speed);
      e.damage = ej.value("damage", e.damage);
      e.castEvery = ej.value("castEvery", e.castEvery);
      e.tag = ej.value("tag", "");
      for (const json &sj : ej.value("spells", json::array())) {
        Spell s;
        s.name = sj.value("name", "");
        SpellJson::Read(sj, s);
        if (int format = sj.value("format", 1); format < SpellJson::FORMAT)
          SpellJson::MigrateLegacyIds(s, format);
        e.spells.push_back(std::move(s));
      }
      r.enemies.push_back(std::move(e));
    }
    r.npcs.clear();
    for (const json &nj : j.value("npcs", json::array())) {
      NpcDef n;
      n.name = nj.value("name", n.name);
      n.tag = nj.value("tag", "");
      n.pos = Vec(nj.at("pos"));
      n.dialogue.clear();
      for (const json &dj : nj.value("dialogue", json::array())) {
        DialogueNode d;
        d.text = dj.value("text", "");
        d.teach = dj.value("teach", "");
        d.teachSigil = dj.value("teachSigil", false);
        for (const json &rj : dj.value("replies", json::array()))
          if (d.replies.size() < MAX_REPLIES)
            d.replies.push_back({rj.value("text", ""), rj.value("next", -1)});
        n.dialogue.push_back(std::move(d));
      }
      if (n.dialogue.empty())
        n.dialogue.push_back({});
      r.npcs.push_back(std::move(n));
    }
    r.conditions.clear();
    for (const json &cj : j.value("conditions", json::array())) {
      ConditionDef c;
      c.kind = static_cast<ConditionKind>(std::clamp(
          cj.value("kind", 0), 0, static_cast<int>(ConditionKind::Count) - 1));
      c.tag = cj.value("tag", "");
      if (auto rg = cj.find("region"); rg != cj.end() && rg->size() == 4)
        c.region = {(*rg)[0].get<float>(), (*rg)[1].get<float>(),
                    (*rg)[2].get<float>(), (*rg)[3].get<float>()};
      c.share = std::clamp(cj.value("share", c.share), 0.05f, 1.0f);
      c.element = static_cast<Element>(std::clamp(
          cj.value("element", static_cast<int>(c.element)), 0,
          static_cast<int>(Element::COUNT) - 1));
      c.amount = std::max(1, cj.value("amount", c.amount));
      c.node = cj.value("node", -1);
      c.hint = cj.value("hint", "");
      r.conditions.push_back(std::move(c));
    }
    r.sealed = j.value("sealed", std::array<bool, EDGES>{});
    return true;
  } catch (const std::exception &e) {
    error = std::string("bad room: ") + e.what();
    return false;
  }
}

json ToJson(const Save &s) {
  json gates = json::array();
  for (auto [room, i] : s.gates)
    gates.push_back({Pos(room), i});
  json shrines = json::array();
  for (auto [room, i] : s.shrinesTaken)
    shrines.push_back({Pos(room), i});
  json visited = json::array();
  for (RoomPos p : s.visited)
    visited.push_back(Pos(p));
  json j{{"glyphs", s.glyphs},
         {"visited", visited},
         {"gates", gates},
         {"shrines", shrines},
         {"slots", s.slots},
         {"talked", s.talked}};
  json cleared = json::array();
  for (RoomPos p : s.cleared)
    cleared.push_back(Pos(p));
  j["cleared"] = cleared;
  if (s.respawn)
    j["respawn"] = {Pos(s.respawn->first), s.respawn->second};
  return j;
}

void FromJson(const json &j, Save &s) {
  s = {};
  try {
    s.glyphs = j.value("glyphs", std::set<std::string>{});
    for (const json &p : j.value("visited", json::array()))
      s.visited.insert(Pos(p));
    for (const json &g : j.value("gates", json::array()))
      s.gates.push_back({Pos(g.at(0)), g.at(1).get<int>()});
    for (const json &g : j.value("shrines", json::array()))
      s.shrinesTaken.insert({Pos(g.at(0)), g.at(1).get<int>()});
    if (j.contains("respawn"))
      s.respawn = {{Pos(j["respawn"].at(0)), j["respawn"].at(1).get<int>()}};
    s.slots = j.value("slots", std::array<std::string, SLOTS>{});
    s.talked = j.value("talked", std::set<std::string>{});
    for (const json &p : j.value("cleared", json::array()))
      s.cleared.insert(Pos(p));
  } catch (const std::exception &) {
    // A damaged save starts over rather than crashing the game
    s = {};
  }
}

std::vector<CampaignDef> LoadAll() {
  std::vector<CampaignDef> out;
  std::error_code ec;
  if (!fs::exists(ROOT, ec))
    return out;
  for (const auto &entry : fs::directory_iterator(ROOT, ec)) {
    if (!entry.is_directory())
      continue;
    auto j = ReadJson((entry.path() / "campaign.json").string());
    CampaignDef c;
    std::string error;
    if (!j || !FromJson(*j, c, error))
      continue;
    c.id = entry.path().filename().string();
    out.push_back(std::move(c));
  }
  std::sort(out.begin(), out.end(),
            [](const CampaignDef &a, const CampaignDef &b) { return a.name < b.name; });
  return out;
}

bool SaveDef(const CampaignDef &c, std::string &error) {
  return WriteJson(Dir(c.id) + "/campaign.json", ToJson(c), error);
}

bool Remove(const std::string &id, std::string &error) {
  std::error_code ec;
  fs::remove_all(Dir(id), ec);
  if (ec)
    error = ec.message();
  return !ec;
}

std::string NewId(const std::string &name) {
  std::string id;
  for (char ch : name)
    if (std::isalnum(static_cast<unsigned char>(ch)))
      id += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    else if (!id.empty() && id.back() != '-')
      id += '-';
  while (!id.empty() && id.back() == '-')
    id.pop_back();
  if (id.empty())
    id = "campaign";
  std::string out = id;
  for (int n = 2; fs::exists(Dir(out)); ++n)
    out = id + "-" + std::to_string(n);
  return out;
}

RoomDef BlankRoom(RoomPos pos) {
  RoomDef r;
  r.pos = pos;
  r.terrain.name = "room";
  r.terrain.cells.assign(static_cast<size_t>(GRID_W) * GRID_H,
                         static_cast<uint8_t>(Element::AIR));
  for (int y = GRID_H - 12; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      r.terrain.cells[static_cast<size_t>(y) * GRID_W + x] =
          static_cast<uint8_t>(Element::EARTH);
  return r;
}

std::optional<RoomDef> LoadRoom(const std::string &id, RoomPos pos,
                                std::string &error) {
  auto j = ReadJson(RoomFile(id, pos));
  if (!j) {
    error = "room file missing";
    return std::nullopt;
  }
  RoomDef r;
  if (!FromJson(*j, r, error))
    return std::nullopt;
  r.pos = pos;
  return r;
}

bool SaveRoom(const std::string &id, const RoomDef &room, std::string &error) {
  return WriteJson(RoomFile(id, room.pos), ToJson(room), error);
}

bool RemoveRoom(const std::string &id, RoomPos pos, std::string &error) {
  std::error_code ec;
  fs::remove(RoomFile(id, pos), ec);
  if (ec)
    error = ec.message();
  return !ec;
}

std::optional<Save> LoadSave(const std::string &id) {
  auto j = ReadJson(Dir(id) + "/save.json");
  if (!j)
    return std::nullopt;
  Save s;
  FromJson(*j, s);
  return s;
}

bool WriteSave(const std::string &id, const Save &save, std::string &error) {
  return WriteJson(Dir(id) + "/save.json", ToJson(save), error);
}

void ClearSave(const std::string &id) {
  std::error_code ec;
  fs::remove(Dir(id) + "/save.json", ec);
  fs::remove_all(BackpackDir(id), ec);
}

std::optional<std::string> ImportBackground(const std::string &id,
                                            const std::string &path,
                                            std::string &error) {
  if (!IsFileExtension(path.c_str(), ".png")) {
    error = "backgrounds are PNG files";
    return std::nullopt;
  }
  Image image = LoadImage(path.c_str());
  if (!image.data) {
    error = "can't read " + path;
    return std::nullopt;
  }
  ImageResize(&image, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE);
  std::string name = fs::path(path).stem().string();
  std::string file = name + ".png";
  std::error_code ec;
  fs::create_directories(Dir(id) + "/backgrounds", ec);
  for (int n = 2; fs::exists(BackgroundPath(id, file)); ++n)
    file = name + "-" + std::to_string(n) + ".png";
  bool ok = ExportImage(image, BackgroundPath(id, file).c_str());
  UnloadImage(image);
  if (!ok) {
    error = "can't save the background";
    return std::nullopt;
  }
  return file;
}

} // namespace Campaign
