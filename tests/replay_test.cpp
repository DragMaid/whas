#include "whas/engine/simulation.h"
#include "whas/game/replay.h"
#include "whas/game/replay_store.h"
#include "whas/spell/spell_library.h"
#include <filesystem>
#include <catch2/catch_test_macros.hpp>
#include <fstream>

// A real match between two bot clients, recorded by the server (see the
// "[.e2e]" test). Re-simulating it must reproduce every hash the clients
// reported. If a deliberate simulation change breaks this, record a new one:
// run the e2e test and save GET /api/matches/{id}/replay here.
namespace {
nlohmann::json LoadReplay() {
  std::ifstream file(WHAS_SOURCE_DIR "/tests/replays/bot_match.json");
  REQUIRE(file);
  return nlohmann::json::parse(file);
}
} // namespace

TEST_CASE("a recorded match replays to the same hashes", "[replay]") {
  ReplayPlayer player;
  std::string error;
  REQUIRE(player.Load(LoadReplay(), error));
  Simulation sim;
  std::vector<std::string> report;
  int mismatches = player.VerifyAll(sim, &report);
  for (const auto &line : report)
    UNSCOPED_INFO(line);
  REQUIRE(player.TurnCount() > 5);
  REQUIRE(player.Checked() == 2 * player.TurnCount());
  REQUIRE(mismatches == 0);
  REQUIRE(player.Finished());
}

TEST_CASE("a replay catches a changed plan", "[replay]") {
  nlohmann::json replay = LoadReplay();
  // Slot 0 stands still on the first turn instead of what it did
  replay["turns"][0]["plans"][0] = R"({"v":1,"runs":[]})";
  ReplayPlayer player;
  std::string error;
  REQUIRE(player.Load(replay, error));
  Simulation sim;
  REQUIRE(player.VerifyAll(sim) > 0);
}

TEST_CASE("finished matches are kept on this machine", "[replay]") {
  std::string dir = std::string(WHAS_SOURCE_DIR) + "/build/test_replays";
  std::filesystem::remove_all(dir);
  ReplayStore::SetDirectory(dir);

  nlohmann::json replay = LoadReplay();
  int slot = 1;
  std::string error;
  REQUIRE(ReplayStore::Save(replay, slot, slot, error));
  auto entries = ReplayStore::List();
  REQUIRE(entries.size() == 1);
  REQUIRE(entries[0].matchId == replay["matchId"].get<int64_t>());
  REQUIRE(entries[0].result == 1);
  REQUIRE(entries[0].slot == 1);

  // What was saved still plays: the local header doesn't get in the way
  auto stored = ReplayStore::Load(entries[0].path);
  REQUIRE(stored);
  ReplayPlayer player;
  REQUIRE(player.Load(*stored, error));
  REQUIRE(player.TurnCount() == static_cast<int>(replay["turns"].size()));

  REQUIRE(ReplayStore::Remove(entries[0].path));
  REQUIRE(ReplayStore::List().empty());
  ReplayStore::SetDirectory("data/replays");
}

TEST_CASE("copied spells are recognised by their drawing", "[replay]") {
  ReplayPlayer player;
  std::string error;
  REQUIRE(player.Load(LoadReplay(), error));
  const auto &cards = player.Cards();
  REQUIRE_FALSE(cards[0].empty());
  REQUIRE(cards[0][0][0]);
  const Spell &spell = cards[0][0][0]->spell;
  Spell renamed = spell;
  renamed.name = "Something else";
  REQUIRE(SpellLibrary::SameDrawing(spell, renamed));
  Spell moved = spell;
  REQUIRE_FALSE(moved.glyphs.empty());
  moved.glyphs[0].position.x += 5;
  REQUIRE_FALSE(SpellLibrary::SameDrawing(spell, moved));
}
