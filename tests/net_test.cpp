#include "whas/constants.h"
#include "whas/core/sha256.h"
#include "whas/engine/simulation.h"
#include "whas/game/rts.h"
#include "whas/net/lockstep_client.h"
#include "whas/net/net_client.h"
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

TEST_CASE("sha256 matches the standard test vectors", "[net]") {
  REQUIRE(Sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE(Sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  // Spans several 64-byte blocks
  REQUIRE(Sha256Hex(std::string(1000, 'a')) ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
  REQUIRE(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

namespace {

Spell MakeSpell(const char *name, const char *sigil, float sigilScale, int signs) {
  Spell spell;
  spell.name = name;
  spell.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, sigilScale, 0.0f});
  for (int i = 0; i < signs; ++i)
    spell.glyphs.push_back({"column", GlyphKind::Sign, {0, -100}, 1.0f, 0.0f});
  return spell;
}

// Two plain spells cast together, with a crushing, cooling outer ring
Spell MakeLayered() {
  Spell spell;
  spell.name = "Storm";
  Spell sand = MakeSpell("Sand", "sand", 1.0f, 2);
  sand.glyphs.push_back({"orb", GlyphKind::Sign, {80, 0}, 1.0f, 0.0f});
  Spell water = MakeSpell("Water", "water", 1.0f, 2);
  water.glyphs.push_back({"dragon", GlyphKind::Sigil, {0, 80}, 1.0f, 0.0f});
  spell.components = {{"Sand", sand.glyphs, {-60, 0}, 0.3f, 0.0f},
                      {"Water", water.glyphs, {60, 0}, 0.3f, -45.0f}};
  spell.glyphs = {{"crushing", GlyphKind::Sign, {0, -210}, 1.0f, 0.0f},
                  {"cooling", GlyphKind::Sign, {0, 210}, 0.5f, 180.0f}};
  return spell;
}

// A player without a screen: walks at the opponent and fires everything
struct Bot {
  Simulation sim;
  Match::State state;
  LockstepClient client;

  explicit Bot(const std::string &identity) {
    client.SetIdentityFile(identity);
    LockstepClient::Library lib;
    lib.spells = {{"bolt", MakeSpell("Bolt", "fire", 1.6f, 3)},
                  {"rock", MakeSpell("Rock", "rock", 1.5f, 4)},
                  {"storm", MakeLayered()}};
    Deck deck;
    deck.id = "bot-deck";
    deck.name = "Bot";
    deck.slots = {"storm", "bolt", "rock", "", "", ""};
    lib.decks = {deck};
    lib.match.deckIds = {"bot-deck", "bot-deck", "bot-deck"};
    client.SetLibrary(lib);
  }

  TurnPlan Plan() const {
    const Character &me = state.characters[client.Slot()];
    const Character &them = state.characters[1 - client.Slot()];
    bool right = them.pos.x > me.pos.x;
    TurnPlan plan;
    for (int t = 0; t < 20; ++t) {
      PlanStep step;
      step.input.right = right;
      step.input.left = !right;
      plan.steps.push_back(step);
    }
    // Cast every card at the opponent, one after another
    Vector2 aim{them.Center().x - me.Center().x,
                them.Center().y - 3.0f - me.Center().y};
    float len = std::hypot(aim.x, aim.y);
    aim = {aim.x / len, aim.y / len};
    int used = 20;
    for (const auto &card : client.Cards(client.Slot())) {
      if (!card || !card->stats.valid)
        continue;
      int ticks = TurnController::CastTicks(card->stats);
      if (used + ticks > TurnController::TURN_TICKS)
        break;
      PlanStep cast;
      PlannedCast c = PlannedCast::Local(card->spell, aim);
      c.stats = card->stats;
      c.spellId = card->id;
      cast.casts.push_back(c);
      plan.steps.push_back(cast);
      for (int i = 1; i < ticks; ++i)
        plan.steps.push_back({});
      used += ticks;
    }
    return plan;
  }

  // Real time: walk at the opponent and fire whatever has cooled down
  Rts::Controller rts;
  int rtsRound = -1;
  void TickRts() {
    if (client.Round() != rtsRound) {
      rtsRound = client.Round();
      rts.BeginRound();
    }
    while (client.RtsReady()) {
      const Character &me = state.characters[client.Slot()];
      const Character &them = state.characters[1 - client.Slot()];
      Vector2 aim{them.Center().x - me.Center().x,
                  them.Center().y - 3.0f - me.Center().y};
      float len = std::max(0.001f, std::hypot(aim.x, aim.y));
      aim = {aim.x / len, aim.y / len};
      for (const auto &card : client.Cards(client.Slot())) {
        if (!card || !card->stats.valid)
          continue;
        PlannedCast c = PlannedCast::Local(card->spell, aim);
        c.stats = card->stats;
        c.spellId = card->id;
        rts.QueueCast(c, card->id, client.RtsInputTick());
      }
      CharacterInput input;
      input.right = them.pos.x > me.pos.x + 30;
      input.left = them.pos.x < me.pos.x - 30;
      client.RtsStep(sim, state, rts.TakeStep(input, {}));
    }
  }

  void Tick() {
    client.Update(sim, state);
    if (client.GetPhase() == LockstepClient::Phase::Realtime)
      TickRts();
    if (client.GetPhase() == LockstepClient::Phase::Planning)
      client.SubmitPlan(Plan());
    if (client.GetPhase() == LockstepClient::Phase::Executing)
      client.StepExecution(sim, state, TurnController::TURN_TICKS);
  }
};

template <typename Pred> bool Until(Bot &a, Bot &b, Pred done, int seconds) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < end) {
    a.Tick();
    b.Tick();
    if (done())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

} // namespace

// Needs a running server: WHAS_SERVER=ws://localhost:8080/ws whas_tests "[.e2e]"
TEST_CASE("two clients play a whole match through the server", "[.e2e]") {
  const char *url = std::getenv("WHAS_SERVER");
  REQUIRE(url);
  using Phase = LockstepClient::Phase;
  std::string dir = std::string(WHAS_SOURCE_DIR) + "/build/e2e";
  std::filesystem::create_directories(dir);
  std::filesystem::remove(dir + "/a.json");
  std::filesystem::remove(dir + "/b.json");
  Bot a(dir + "/a.json"), b(dir + "/b.json");
  a.client.Connect(url);
  b.client.Connect(url);
  REQUIRE(Until(a, b, [&] {
    return a.client.GetPhase() == Phase::Ready && b.client.GetPhase() == Phase::Ready;
  }, 10));

  a.client.CreateLobby();
  REQUIRE(Until(a, b, [&] { return a.client.GetPhase() == Phase::Hosting; }, 10));
  b.client.JoinLobby(a.client.LobbyCode());
  REQUIRE(Until(a, b, [&] {
    return a.client.GetPhase() == Phase::MatchOver && b.client.GetPhase() == Phase::MatchOver;
  }, 600));

  for (auto &n : a.client.TakeNotices())
    UNSCOPED_INFO("a: " << n);
  for (auto &n : b.client.TakeNotices())
    UNSCOPED_INFO("b: " << n);
  REQUIRE(a.client.Desyncs() == 0);
  REQUIRE(b.client.Desyncs() == 0);
  REQUIRE(a.client.MatchWinner() == b.client.MatchWinner());
  REQUIRE(a.client.RoundsWon() == b.client.RoundsWon());
  REQUIRE(a.client.MatchEndReason() == "rounds");
  // Both clients ended in the same world
  REQUIRE(Match::Hash(a.sim, a.state) == Match::Hash(b.sim, b.state));
}

TEST_CASE("two clients play a real-time match through the server", "[.e2e]") {
  const char *url = std::getenv("WHAS_SERVER");
  REQUIRE(url);
  using Phase = LockstepClient::Phase;
  std::string dir = std::string(WHAS_SOURCE_DIR) + "/build/e2e";
  std::filesystem::create_directories(dir);
  std::filesystem::remove(dir + "/rts-a.json");
  std::filesystem::remove(dir + "/rts-b.json");
  Bot a(dir + "/rts-a.json"), b(dir + "/rts-b.json");
  a.client.Connect(url);
  b.client.Connect(url);
  REQUIRE(Until(a, b, [&] {
    return a.client.GetPhase() == Phase::Ready && b.client.GetPhase() == Phase::Ready;
  }, 10));

  MatchOptions options;
  options.rts = true;
  a.client.CreateLobby(options);
  REQUIRE(Until(a, b, [&] { return a.client.GetPhase() == Phase::Hosting; }, 10));
  b.client.JoinLobby(a.client.LobbyCode());
  REQUIRE(Until(a, b, [&] {
    return a.client.GetPhase() == Phase::MatchOver && b.client.GetPhase() == Phase::MatchOver;
  }, 900));

  for (auto &n : a.client.TakeNotices())
    UNSCOPED_INFO("a: " << n);
  for (auto &n : b.client.TakeNotices())
    UNSCOPED_INFO("b: " << n);
  REQUIRE(a.client.Options().rts);
  REQUIRE(a.client.MatchEndReason() == "rounds");
  REQUIRE(a.client.MatchWinner() == b.client.MatchWinner());
  REQUIRE(a.client.RoundsWon() == b.client.RoundsWon());
  REQUIRE(Match::Hash(a.sim, a.state) == Match::Hash(b.sim, b.state));
}

// A sparring partner for trying online play by hand:
//   WHAS_SERVER=ws://localhost:8080/ws whas_tests "[.bot]"
// then press Quick match in the game.
TEST_CASE("bot opponent waits in the queue and plays one match", "[.bot]") {
  const char *url = std::getenv("WHAS_SERVER");
  REQUIRE(url);
  using Phase = LockstepClient::Phase;
  std::string dir = std::string(WHAS_SOURCE_DIR) + "/build/e2e";
  std::filesystem::create_directories(dir);
  Bot bot(dir + "/bot.json");
  bot.client.Connect(url);
  auto until = [&](auto done, int seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end && !done()) {
      bot.Tick();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
  };
  REQUIRE(until([&] { return bot.client.GetPhase() == Phase::Ready; }, 10));
  bot.client.QuickMatch();
  REQUIRE(until([&] { return bot.client.GetPhase() == Phase::MatchOver; }, 1800));
  REQUIRE(bot.client.Desyncs() == 0);
}

TEST_CASE("server addresses become WebSocket URLs", "[net]") {
  REQUIRE(NetClient::WebSocketUrl("https://abc.ngrok-free.app") ==
          "wss://abc.ngrok-free.app/ws");
  REQUIRE(NetClient::WebSocketUrl("http://localhost:8080") ==
          "ws://localhost:8080/ws");
  REQUIRE(NetClient::WebSocketUrl("ws://localhost:8080/ws") ==
          "ws://localhost:8080/ws");
  REQUIRE(NetClient::WebSocketUrl("localhost:8080") == "ws://localhost:8080/ws");
  REQUIRE(NetClient::HttpBase("wss://abc.ngrok-free.app/ws") ==
          "https://abc.ngrok-free.app");
}

TEST_CASE("the client follows a redirect to the real server", "[net]") {
  // The real server: echoes one message back
  ix::WebSocketServer ws(18731, "127.0.0.1");
  ws.setOnClientMessageCallback(
      [](std::shared_ptr<ix::ConnectionState>, ix::WebSocket &socket,
         const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Message)
          socket.sendText(msg->str);
      });
  REQUIRE(ws.listen().first);
  ws.start();
  // A tunnel in front of it that sends everyone elsewhere
  ix::HttpServer tunnel(18732, "127.0.0.1");
  tunnel.setOnConnectionCallback(
      [](ix::HttpRequestPtr, std::shared_ptr<ix::ConnectionState>) {
        ix::WebSocketHttpHeaders headers;
        headers["Location"] = "http://127.0.0.1:18731/ws";
        return std::make_shared<ix::HttpResponse>(
            308, "Permanent Redirect", ix::HttpErrorCode::Ok, headers, "");
      });
  REQUIRE(tunnel.listen().first);
  tunnel.start();

  NetClient client;
  client.Connect("http://127.0.0.1:18732");
  bool echoed = false;
  for (int i = 0; i < 300 && !echoed; ++i) {
    for (const nlohmann::json &j : client.Poll()) {
      if (j["type"] == "_open")
        client.Send({{"type", "ping"}});
      echoed = echoed || j["type"] == "ping";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  client.Close();
  tunnel.stop();
  ws.stop();
  REQUIRE(echoed);
}
