#pragma once
#include "whas/core/config.h"
#include "whas/game/arena_gen.h"
#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class Simulation;

// A player-made arena: the terrain as one element per cell, where the two
// players start and the world settings it plays by (as the difference from
// the defaults). The generator knobs it was started from are kept so the
// editor can show them again.
struct MapDef {
  static constexpr int FORMAT = 1;

  std::string id; // file name in data/maps; never shown
  std::string name;
  uint64_t genSeed = 1;
  ArenaGen::Params gen;
  std::array<Vector2, 2> spawns{}; // character top-left, in cells
  std::vector<uint8_t> cells;      // GRID_W * GRID_H elements, row by row
  nlohmann::json settings = nlohmann::json::object(); // ConfigDiff

  SimulationConfig Config() const;
};

namespace Maps {

// Largest encoded map a lobby accepts
constexpr size_t MAX_ENCODED_BYTES = 64 * 1024;

// The world's terrain as it stands (spells and particles are left out)
std::vector<uint8_t> CaptureCells(const Simulation &sim);

// A fresh world for a round on this map: its settings, then its cells.
// Rock is anchored like generated terrain.
void Build(Simulation &sim, const MapDef &map, uint64_t seed);

nlohmann::json ToJson(const MapDef &map);
// False (with a reason) on anything malformed or out of range
bool FromJson(const nlohmann::json &j, MapDef &map, std::string &error);

} // namespace Maps

// One entry of a room's map pool
struct MapSpec {
  std::optional<MapDef> custom; // empty: a generated arena from the seed
};

// What a room (or a solo match) plays by. Round r is played on
// pool[r % pool.size()]; an empty pool means random arenas.
struct MatchOptions {
  static constexpr int MAX_POOL = 3;

  std::vector<MapSpec> pool;
  bool chaos = false; // no limits on signs and sigils
  bool rts = false;   // real time instead of planned turns

  const MapSpec *MapFor(int round) const;
};

nlohmann::json OptionsToJson(const MatchOptions &options);
bool OptionsFromJson(const nlohmann::json &j, MatchOptions &options,
                     std::string &error);

// data/maps/<id>.json, with a <id>.png thumbnail next to it
class MapStore {
public:
  static constexpr const char *MAPS_DIR = "data/maps";

  std::vector<MapDef> LoadAll() const;
  bool Save(const MapDef &map, std::string &error) const;
  bool Remove(const std::string &id, std::string &error) const;
  static std::string ThumbnailPath(const std::string &id);
  // A new id nobody has used yet
  static std::string NewId();
};
