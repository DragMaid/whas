#include "whas/game/map.h"
#include "whas/constants.h"
#include "whas/core/bytes.h"
#include "whas/core/config_json.h"
#include "whas/engine/simulation.h"
#include "whas/game/character.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr size_t CELL_COUNT = static_cast<size_t>(GRID_W) * GRID_H;

bool Placeable(uint8_t e) {
  return e < static_cast<uint8_t>(Element::COUNT) &&
         static_cast<Element>(e) != Element::LIGHT;
}

// Runs of one element: [count as a varint][element]
std::vector<uint8_t> Rle(const std::vector<uint8_t> &cells) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < cells.size();) {
    size_t j = i;
    while (j < cells.size() && cells[j] == cells[i])
      ++j;
    for (size_t run = j - i;; run >>= 7) {
      if (run < 0x80) {
        out.push_back(static_cast<uint8_t>(run));
        break;
      }
      out.push_back(static_cast<uint8_t>(run & 0x7F) | 0x80);
    }
    out.push_back(cells[i]);
    i = j;
  }
  return out;
}

bool Unrle(const std::vector<uint8_t> &in, std::vector<uint8_t> &cells) {
  cells.clear();
  size_t i = 0;
  while (i < in.size()) {
    size_t run = 0;
    for (int shift = 0;; shift += 7) {
      if (i >= in.size() || shift > 28)
        return false;
      uint8_t b = in[i++];
      run |= static_cast<size_t>(b & 0x7F) << shift;
      if (!(b & 0x80))
        break;
    }
    if (i >= in.size() || run == 0 || cells.size() + run > CELL_COUNT ||
        !Placeable(in[i]))
      return false;
    cells.insert(cells.end(), run, in[i++]);
  }
  return cells.size() == CELL_COUNT;
}

bool InGrid(Vector2 spawn) {
  return std::isfinite(spawn.x) && std::isfinite(spawn.y) && spawn.x >= 0 &&
         spawn.y >= 0 && spawn.x + Character::WIDTH <= GRID_W &&
         spawn.y + Character::HEIGHT <= GRID_H;
}

} // namespace

SimulationConfig MapDef::Config() const { return ConfigFromDiff(settings); }

namespace Maps {

std::vector<uint8_t> CaptureCells(const Simulation &sim) {
  std::vector<uint8_t> cells(CELL_COUNT);
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      Element e = sim.GetCell(x, y).element;
      // Smoke and flashes are passing effects, not terrain
      bool keep = Placeable(static_cast<uint8_t>(e)) && e != Element::SMOKE;
      cells[static_cast<size_t>(y) * GRID_W + x] =
          static_cast<uint8_t>(keep ? e : Element::AIR);
    }
  return cells;
}

void Build(Simulation &sim, const MapDef &map, uint64_t seed) {
  sim.GetConfig() = map.Config();
  sim.Restart(seed);
  if (map.cells.size() != CELL_COUNT)
    return;
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      auto e = static_cast<Element>(map.cells[static_cast<size_t>(y) * GRID_W + x]);
      if (e != Element::AIR)
        sim.Paint(x, y, e, 0);
    }
  ArenaGen::AnchorRock(sim);
}

json ToJson(const MapDef &map) {
  json j;
  j["format"] = MapDef::FORMAT;
  j["id"] = map.id;
  j["name"] = map.name;
  j["gen"] = {{"seed", std::to_string(map.genSeed)},
              {"biome", static_cast<int>(map.gen.biome)},
              {"hills", map.gen.hills},
              {"waterRise", map.gen.waterRise},
              {"vegetation", map.gen.vegetation},
              {"rocks", map.gen.rocks}};
  j["spawns"] = json::array();
  for (Vector2 s : map.spawns)
    j["spawns"].push_back({s.x, s.y});
  j["cells"] = Base64::Encode(Rle(map.cells));
  j["settings"] = map.settings.is_object() ? map.settings : json::object();
  return j;
}

bool FromJson(const json &j, MapDef &map, std::string &error) {
  try {
    if (!j.is_object() || j.value("format", 0) != MapDef::FORMAT) {
      error = "unknown map format";
      return false;
    }
    map.id = j.value("id", "");
    map.name = j.value("name", "");
    if (map.name.empty() || map.name.size() > 32) {
      error = "map name must be 1-32 characters";
      return false;
    }
    const json &gen = j.at("gen");
    map.genSeed = std::stoull(gen.value("seed", "1"));
    int biome = gen.value("biome", 0);
    if (biome < 0 || biome >= static_cast<int>(ArenaGen::Biome::COUNT))
      biome = 0;
    map.gen.biome = static_cast<ArenaGen::Biome>(biome);
    map.gen.hills = std::clamp(gen.value("hills", 100), 0, 300);
    map.gen.waterRise = std::clamp(gen.value("waterRise", 0), -GRID_H, GRID_H);
    map.gen.vegetation = std::clamp(gen.value("vegetation", 100), 0, 400);
    map.gen.rocks = std::clamp(gen.value("rocks", 100), 0, 400);

    const json &spawns = j.at("spawns");
    if (!spawns.is_array() || spawns.size() != 2) {
      error = "a map needs two spawns";
      return false;
    }
    for (int i = 0; i < 2; ++i) {
      map.spawns[i] = {spawns[i].at(0).get<float>(),
                       spawns[i].at(1).get<float>()};
      if (!InGrid(map.spawns[i])) {
        error = "spawn outside the world";
        return false;
      }
    }

    std::vector<uint8_t> rle;
    if (!Base64::Decode(j.at("cells").get<std::string>(), rle) ||
        !Unrle(rle, map.cells)) {
      error = "map cells are damaged";
      return false;
    }
    map.settings = j.value("settings", json::object());
    if (!map.settings.is_object())
      map.settings = json::object();
    return true;
  } catch (const std::exception &e) {
    error = std::string("bad map: ") + e.what();
    return false;
  }
}

} // namespace Maps

const MapSpec *MatchOptions::MapFor(int round) const {
  if (pool.empty())
    return nullptr;
  return &pool[static_cast<size_t>(round) % pool.size()];
}

json OptionsToJson(const MatchOptions &options) {
  json maps = json::array();
  for (const MapSpec &spec : options.pool) {
    if (spec.custom)
      maps.push_back({{"kind", "custom"}, {"map", Maps::ToJson(*spec.custom)}});
    else
      maps.push_back({{"kind", "random"}});
  }
  return {{"maps", maps}, {"chaos", options.chaos}, {"rts", options.rts}};
}

bool OptionsFromJson(const json &j, MatchOptions &options, std::string &error) {
  options = {};
  if (j.is_null())
    return true;
  if (!j.is_object()) {
    error = "bad match options";
    return false;
  }
  options.chaos = j.value("chaos", false);
  options.rts = j.value("rts", false);
  const json maps = j.value("maps", json::array());
  if (!maps.is_array() || maps.size() > MatchOptions::MAX_POOL) {
    error = "a room takes at most 3 maps";
    return false;
  }
  for (const json &m : maps) {
    MapSpec spec;
    if (m.value("kind", "random") == "custom") {
      MapDef def;
      if (!Maps::FromJson(m.at("map"), def, error))
        return false;
      spec.custom = std::move(def);
    }
    options.pool.push_back(std::move(spec));
  }
  return true;
}

std::vector<MapDef> MapStore::LoadAll() const {
  std::vector<MapDef> maps;
  if (!fs::exists(MAPS_DIR))
    return maps;
  for (const auto &entry : fs::directory_iterator(MAPS_DIR)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
      continue;
    std::ifstream file(entry.path());
    json j = json::parse(file, nullptr, false);
    MapDef map;
    std::string error;
    if (j.is_discarded() || !Maps::FromJson(j, map, error))
      continue;
    map.id = entry.path().stem().string();
    maps.push_back(std::move(map));
  }
  std::sort(maps.begin(), maps.end(),
            [](const MapDef &a, const MapDef &b) { return a.name < b.name; });
  return maps;
}

bool MapStore::Save(const MapDef &map, std::string &error) const {
  if (map.id.empty() || map.name.empty()) {
    error = "Give the map a name.";
    return false;
  }
  fs::create_directories(MAPS_DIR);
  std::ofstream file(fs::path(MAPS_DIR) / (map.id + ".json"));
  if (!file) {
    error = "Failed to write the map file.";
    return false;
  }
  file << Maps::ToJson(map).dump();
  return true;
}

bool MapStore::Remove(const std::string &id, std::string &error) const {
  std::error_code ec;
  fs::remove(ThumbnailPath(id), ec);
  if (!fs::remove(fs::path(MAPS_DIR) / (id + ".json"), ec)) {
    error = ec ? ec.message() : "Map file not found.";
    return false;
  }
  return true;
}

std::string MapStore::ThumbnailPath(const std::string &id) {
  return (fs::path(MAPS_DIR) / (id + ".png")).string();
}

std::string MapStore::NewId() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  char buf[32];
  std::snprintf(buf, sizeof buf, "map-%llx", static_cast<unsigned long long>(stamp));
  std::string id = buf;
  while (fs::exists(fs::path(MAPS_DIR) / (id + ".json")))
    id += "x";
  return id;
}
