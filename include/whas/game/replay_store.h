#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// Replays kept on this machine: data/replays/<matchId>.json, the server's
// GET /api/matches/{id}/replay as it was after the match, plus a "local"
// header (when it was saved, which slot was ours, how it ended) so the list
// can be shown without re-reading every turn.
class ReplayStore {
public:
  // data/replays unless changed (tests use a scratch folder)
  static void SetDirectory(std::string dir);
  static const std::string &Directory();

  struct Entry {
    std::string path;
    int64_t matchId = 0;
    int64_t savedAt = 0; // unix seconds
    int slot = 0;        // ours
    int64_t opponentId = 0;
    int result = 0; // 1 won, -1 lost, 0 draw/void
    int roundsWon[2]{};
    std::vector<std::string> maps; // names, "Random arena" for generated
  };

  // Adds the header and writes the file; the replay as the server sent it
  static bool Save(nlohmann::json replay, int slot, int winner,
                   std::string &error);
  // Newest first
  static std::vector<Entry> List();
  static std::optional<nlohmann::json> Load(const std::string &path);
  static bool Remove(const std::string &path);
};
