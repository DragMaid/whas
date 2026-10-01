#include "whas/game/replay_store.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::string g_dir = "data/replays";
}

void ReplayStore::SetDirectory(std::string dir) { g_dir = std::move(dir); }
const std::string &ReplayStore::Directory() { return g_dir; }

bool ReplayStore::Save(json replay, int slot, int winner, std::string &error) {
  if (!replay.is_object() || !replay.contains("matchId")) {
    error = "not a replay";
    return false;
  }
  int64_t id = replay.value("matchId", int64_t{0});
  int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  replay["local"] = {{"savedAt", now}, {"slot", slot}, {"winner", winner}};
  std::error_code ec;
  fs::create_directories(g_dir, ec);
  std::ofstream file(fs::path(g_dir) / (std::to_string(id) + ".json"));
  if (!file) {
    error = "couldn't write the replay";
    return false;
  }
  file << replay.dump();
  return true;
}

std::optional<json> ReplayStore::Load(const std::string &path) {
  std::ifstream file(path);
  json j = json::parse(file, nullptr, false);
  if (!file || j.is_discarded())
    return std::nullopt;
  return j;
}

bool ReplayStore::Remove(const std::string &path) {
  std::error_code ec;
  return fs::remove(path, ec);
}

std::vector<ReplayStore::Entry> ReplayStore::List() {
  std::vector<Entry> entries;
  std::error_code ec;
  if (!fs::exists(g_dir, ec))
    return entries;
  for (const auto &file : fs::directory_iterator(g_dir, ec)) {
    if (!file.is_regular_file() || file.path().extension() != ".json")
      continue;
    auto j = Load(file.path().string());
    if (!j || !j->is_object())
      continue;
    try {
      Entry e;
      e.path = file.path().string();
      e.matchId = j->value("matchId", int64_t{0});
      const json local = j->value("local", json::object());
      e.savedAt = local.value("savedAt", int64_t{0});
      e.slot = std::clamp(local.value("slot", 0), 0, 1);
      int winner = local.value("winner", -1);
      e.result = winner == e.slot ? 1 : winner == 1 - e.slot ? -1 : 0;
      for (const json &p : j->value("players", json::array())) {
        int s = p.value("slot", 0);
        if (s == 1 - e.slot)
          e.opponentId = p.value("playerId", int64_t{0});
        if (s == 0 || s == 1)
          e.roundsWon[s] = p.value("roundsWon", 0);
      }
      const json options = j->value("options", json::object());
      for (const json &m : options.value("maps", json::array()))
        e.maps.push_back(m.value("kind", "") == "custom"
                             ? m.at("map").value("name", "Custom map")
                             : "Random arena");
      entries.push_back(std::move(e));
    } catch (const std::exception &) {
      continue;
    }
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return a.savedAt != b.savedAt ? a.savedAt > b.savedAt : a.matchId > b.matchId;
  });
  return entries;
}
