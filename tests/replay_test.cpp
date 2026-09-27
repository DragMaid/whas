#include "whas/engine/simulation.h"
#include "whas/game/replay.h"
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
