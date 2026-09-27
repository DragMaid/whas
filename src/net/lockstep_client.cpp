#include "whas/net/lockstep_client.h"
#include "whas/core/sha256.h"
#include "whas/engine/simulation.h"
#include <filesystem>
#include <fstream>
#include <random>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int PROTOCOL = 1;

json GlyphsJson(const Spell &spell) {
  json glyphs = json::array();
  for (const PlacedGlyph &g : spell.glyphs)
    glyphs.push_back({{"assetId", g.assetId},
                      {"kind", g.kind == GlyphKind::Sigil ? "sigil" : "sign"},
                      {"x", g.position.x},
                      {"y", g.position.y},
                      {"scale", g.scale},
                      {"rotation", g.rotationDeg}});
  return glyphs;
}

std::optional<MatchCard> ParseCard(const json &j) {
  if (!j.is_object())
    return std::nullopt;
  MatchCard card;
  card.id = j.at("id").get<int64_t>();
  card.spell.name = j.at("name").get<std::string>();
  for (const auto &g : j.at("glyphs")) {
    PlacedGlyph pg;
    pg.assetId = g.at("assetId").get<std::string>();
    pg.kind = g.at("kind") == "sigil" ? GlyphKind::Sigil : GlyphKind::Sign;
    pg.position = {g.at("x").get<float>(), g.at("y").get<float>()};
    pg.scale = g.at("scale").get<float>();
    pg.rotationDeg = g.at("rotation").get<float>();
    card.spell.glyphs.push_back(pg);
  }
  card.stats = SpellQuant::Dequantize(j.at("stats").get<SpellQuant::Stats>());
  return card;
}

RoundCards ParseRound(const json &j) {
  RoundCards cards{};
  for (int i = 0; i < DECK_SLOTS && i < (int)j.size(); ++i)
    cards[i] = ParseCard(j[i]);
  return cards;
}

std::string RandomNonce() {
  std::random_device rd; // not simulation state: any randomness will do
  static const char *hex = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < 32; ++i)
    s += hex[rd() & 15];
  return s;
}

} // namespace

LockstepClient::LockstepClient(std::string buildId)
    : m_buildId(std::move(buildId)) {}

void LockstepClient::Connect(const std::string &url) {
  m_url = url;
  LoadIdentity();
  m_phase = Phase::Connecting;
  m_net.Connect(url);
}

void LockstepClient::Disconnect() {
  m_net.Close();
  m_phase = Phase::Offline;
}

bool LockstepClient::InMatch() const {
  switch (m_phase) {
  case Phase::Decks:
  case Phase::Waiting:
  case Phase::Planning:
  case Phase::Committed:
  case Phase::Revealed:
  case Phase::Executing:
  case Phase::Reporting:
  case Phase::Resync:
    return true;
  default:
    return false;
  }
}

float LockstepClient::SecondsLeft() const {
  if (m_phase != Phase::Planning)
    return -1.0f;
  auto left = std::chrono::duration<float>(m_deadline - Clock::now()).count();
  return std::max(0.0f, left);
}

std::vector<std::string> LockstepClient::TakeNotices() {
  return std::exchange(m_notices, {});
}

void LockstepClient::Notice(std::string text) {
  m_notices.push_back(std::move(text));
}

void LockstepClient::SetDeadline(int64_t ms) {
  m_deadline = Clock::now() + std::chrono::milliseconds(ms);
}

// ---- Identity ---------------------------------------------------------------

void LockstepClient::LoadIdentity() {
  m_token.clear();
  m_serverDecks.clear();
  std::ifstream file(m_identityFile);
  if (!file)
    return;
  json j = json::parse(file, nullptr, false);
  if (j.is_discarded() || !j.contains("servers") || !j["servers"].contains(m_url))
    return;
  const json &s = j["servers"][m_url];
  m_token = s.value("token", "");
  if (s.contains("decks"))
    for (auto &[local, id] : s["decks"].items())
      m_serverDecks[local] = id.get<int64_t>();
}

void LockstepClient::SaveIdentity() const {
  json j;
  if (std::ifstream file(m_identityFile); file)
    j = json::parse(file, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    j = json::object();
  json decks = json::object();
  for (auto &[local, id] : m_serverDecks)
    decks[local] = id;
  j["servers"][m_url] = {{"token", m_token}, {"decks", decks}};
  std::filesystem::path path(m_identityFile);
  if (path.has_parent_path())
    std::filesystem::create_directories(path.parent_path());
  std::ofstream(m_identityFile) << j.dump(2);
}

// ---- Commands -------------------------------------------------------------

void LockstepClient::SayHello() {
  json hello{{"type", "hello"}, {"buildId", m_buildId}, {"protocol", PROTOCOL}};
  hello["token"] = m_token.empty() ? json(nullptr) : json(m_token);
  m_net.Send(hello);
}

void LockstepClient::QuickMatch() { StartSync("queue"); }
void LockstepClient::CreateLobby() { StartSync("lobby"); }

void LockstepClient::JoinLobby(const std::string &code) {
  m_joinCode = code;
  StartSync("join");
}

void LockstepClient::Rejoin() {
  if (m_runningMatch)
    m_net.Send({{"type", "rejoin"}, {"matchId", *m_runningMatch}});
}

void LockstepClient::CancelWaiting() {
  m_net.Send({{"type", "cancelQueue"}});
  m_phase = Phase::Ready;
  m_lobbyCode.clear();
}

void LockstepClient::Leave() {
  if (InMatch())
    m_net.Send({{"type", "leave"}});
}

// Upload every spell, then the decks used for the three rounds, then do
// what was asked (queue / host / join)
void LockstepClient::StartSync(const char *then) {
  if (m_phase != Phase::Ready)
    return;
  m_afterSync = then;
  m_phase = Phase::Syncing;
  m_syncStage = SyncStage::Spells;
  m_spellIds.clear();
  m_pendingUploads = static_cast<int>(m_library.spells.size());
  for (const LibrarySpell &s : m_library.spells)
    m_net.Send({{"type", "uploadSpell"},
                {"ref", s.ref},
                {"name", s.spell.name},
                {"glyphs", GlyphsJson(s.spell)}});
  if (m_pendingUploads == 0)
    ContinueSync();
}

void LockstepClient::ContinueSync() {
  if (m_syncStage == SyncStage::Spells) {
    if (m_pendingUploads > 0)
      return;
    // Spells are in: upload the decks the three rounds use
    m_syncStage = SyncStage::Decks;
    m_pendingDecks = 0;
    std::vector<std::string> ids;
    for (const std::string &id : m_library.match.deckIds)
      if (std::find(ids.begin(), ids.end(), id) == ids.end())
        ids.push_back(id);
    for (const std::string &id : ids) {
      auto deck = std::find_if(m_library.decks.begin(), m_library.decks.end(),
                               [&](const Deck &d) { return d.id == id; });
      if (deck == m_library.decks.end()) {
        Notice("A round has no deck; pick one under Spells & decks");
        m_phase = Phase::Ready;
        return;
      }
      UploadDeck(*deck);
    }
  }
  if (m_pendingDecks > 0)
    return;

  std::string then = m_afterSync ? m_afterSync : "";
  m_afterSync = nullptr;
  if (then == "queue")
    m_net.Send({{"type", "queue"}});
  else if (then == "lobby")
    m_net.Send({{"type", "createLobby"}});
  else if (then == "join")
    m_net.Send({{"type", "joinLobby"}, {"code", m_joinCode}});
  m_phase = Phase::Ready; // until queued / lobbyCreated / matchFound
}

void LockstepClient::UploadDeck(const Deck &deck) {
  json spellIds = json::array();
  for (const std::string &ref : deck.slots) {
    auto it = m_spellIds.find(ref);
    spellIds.push_back(it == m_spellIds.end() ? 0 : it->second);
  }
  json msg{{"type", "upsertDeck"},
           {"ref", deck.id},
           {"name", deck.name},
           {"spellIds", spellIds}};
  if (auto known = m_serverDecks.find(deck.id); known != m_serverDecks.end())
    msg["deckId"] = known->second;
  m_net.Send(msg);
  ++m_pendingDecks;
}

void LockstepClient::SendMatchDecks() {
  json ids = json::array();
  for (const std::string &local : m_library.match.deckIds) {
    auto it = m_serverDecks.find(local);
    if (it == m_serverDecks.end()) {
      Notice("Your round decks aren't uploaded; leaving the match");
      Leave();
      return;
    }
    ids.push_back(it->second);
  }
  m_net.Send({{"type", "matchDecks"}, {"deckIds", ids}});
}

void LockstepClient::SubmitPlan(const TurnPlan &plan) {
  if (m_phase != Phase::Planning)
    return;
  m_planText = PlanCodec::Encode(plan).dump();
  m_nonce = RandomNonce();
  m_net.Send({{"type", "commit"},
              {"round", m_round},
              {"turn", m_turn},
              {"hash", Sha256Hex(m_planText + m_nonce)}});
  m_phase = Phase::Committed;
}

// ---- Match flow -----------------------------------------------------------

void LockstepClient::BeginRound(int round, const json &decks, Simulation &sim,
                                Match::State &state) {
  for (int s = 0; s < 2; ++s) {
    if ((int)m_roundCards[s].size() <= round)
      m_roundCards[s].resize(round + 1);
    m_roundCards[s][round] = ParseRound(decks.at(s));
    m_cards[s] = m_roundCards[s][round];
  }
  m_round = round;
  state = Match::BeginRound(sim, m_seed, round);
  m_phase = Phase::Waiting;
}

bool LockstepClient::DecodePlan(const std::string &text, int slot,
                                TurnPlan &plan) {
  const RoundCards &cards = m_roundCards[slot][m_round];
  auto resolve = [&cards](int64_t id, Spell &spell, SpellStats &stats) {
    for (const auto &card : cards)
      if (card && card->id == id) {
        spell = card->spell;
        stats = card->stats;
        return true;
      }
    return false;
  };
  std::string error;
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !PlanCodec::Decode(j, resolve, plan, error)) {
    // The server validated it, so this means our deck data is off
    Notice("Couldn't read a plan: " + error);
    plan = {};
    return false;
  }
  return true;
}

bool LockstepClient::StepExecution(Simulation &sim, Match::State &state,
                                   int ticks) {
  if (m_phase != Phase::Executing)
    return false;
  for (; ticks > 0 && m_execTick < TurnController::TURN_TICKS; --ticks)
    Match::ExecuteTick(sim, state, {&m_plans[0], &m_plans[1]}, m_execTick++);
  if (m_execTick < TurnController::TURN_TICKS)
    return false;
  Match::EndTurn(state);
  ReportHash(sim, state);
  return true;
}

void LockstepClient::ReportHash(Simulation &sim, Match::State &state) {
  m_net.Send({{"type", "stateHash"},
              {"round", m_round},
              {"turn", m_turn},
              {"hash", std::to_string(Match::Hash(sim, state))},
              {"winner", Match::RoundWinner(state)}});
  m_phase = Phase::Reporting;
}

// Back after a reconnect: rebuild every round from the seed and all plans
void LockstepClient::CatchUp(const json &msg, Simulation &sim,
                             Match::State &state) {
  m_matchId = msg.at("matchId").get<int64_t>();
  m_seed = std::stoull(msg.at("seed").get<std::string>());
  m_slot = msg.at("slot").get<int>();
  const json &decks = msg.at("decks");
  for (int s = 0; s < 2; ++s) {
    m_roundCards[s].clear();
    if (decks[s].is_array())
      for (const json &round : decks[s])
        m_roundCards[s].push_back(ParseRound(round));
  }
  m_roundsWon = msg.at("roundsWon").get<std::array<int, 2>>();
  const json &current = msg.at("current");
  int currentRound = current.at("round").get<int>();

  for (int r = 0; r <= currentRound && r < (int)m_roundCards[0].size(); ++r) {
    m_round = r;
    state = Match::BeginRound(sim, m_seed, r);
    for (const json &t : msg.at("turns")) {
      if (t.at("round").get<int>() != r)
        continue;
      m_turn = t.at("turn").get<int>();
      for (int s = 0; s < 2; ++s)
        DecodePlan(t.at("plans")[s].get<std::string>(), s, m_plans[s]);
      Match::ExecuteTurn(sim, state, {&m_plans[0], &m_plans[1]});
    }
  }
  for (int s = 0; s < 2; ++s)
    if (m_round < (int)m_roundCards[s].size())
      m_cards[s] = m_roundCards[s][m_round];

  std::string phase = current.at("phase").get<std::string>();
  m_turn = current.at("turn").get<int>();
  SetDeadline(current.value("deadlineMs", 0));
  m_opponentConnected = true;
  if (phase == "decks") {
    m_phase = Phase::Decks;
    SendMatchDecks();
  } else if (phase == "commit" && !current.value("committed", false)) {
    m_phase = Phase::Planning;
  } else if (phase == "hash") {
    ReportHash(sim, state);
  } else {
    // Committed before dropping: that plan is gone, the server will have us
    // stand still this turn
    m_phase = Phase::Revealed;
  }
  Notice("Rejoined the match");
}

void LockstepClient::Update(Simulation &sim, Match::State &state) {
  for (const json &msg : m_net.Poll()) {
    try {
      Handle(msg, sim, state);
    } catch (const json::exception &e) {
      Notice(std::string("Bad message from the server: ") + e.what());
    } catch (const std::exception &e) {
      Notice(std::string("Error: ") + e.what());
    }
  }
}

void LockstepClient::Handle(const json &msg, Simulation &sim,
                            Match::State &state) {
  const std::string type = msg.at("type").get<std::string>();

  if (type == "_open") {
    SayHello();
  } else if (type == "_closed") {
    bool wasInMatch = InMatch();
    m_phase = Phase::Offline;
    Notice(wasInMatch ? "Connection lost - reconnect to rejoin the match"
                      : "Disconnected: " + msg.value("reason", ""));
  } else if (type == "welcome") {
    m_playerId = msg.at("playerId").get<int64_t>();
    if (msg.contains("token") && msg["token"].is_string()) {
      m_token = msg["token"].get<std::string>();
      SaveIdentity();
    }
    m_runningMatch.reset();
    if (msg.contains("runningMatch") && msg["runningMatch"].is_number())
      m_runningMatch = msg["runningMatch"].get<int64_t>();
    m_phase = Phase::Ready;
  } else if (type == "spellAccepted" || type == "spellRejected") {
    if (type == "spellAccepted")
      m_spellIds[msg.at("ref").get<std::string>()] =
          msg.at("spellId").get<int64_t>();
    else
      Notice("Spell \"" + msg.value("ref", "") +
             "\" was not accepted: " + msg.value("reason", ""));
    --m_pendingUploads;
    ContinueSync();
  } else if (type == "deckAccepted") {
    m_serverDecks[msg.at("ref").get<std::string>()] =
        msg.at("deckId").get<int64_t>();
    SaveIdentity();
    --m_pendingDecks;
    ContinueSync();
  } else if (type == "deckRejected") {
    std::string ref = msg.value("ref", "");
    auto deck = std::find_if(m_library.decks.begin(), m_library.decks.end(),
                             [&](const Deck &d) { return d.id == ref; });
    if (msg.value("reason", "") == "no such deck" && m_serverDecks.erase(ref) &&
        deck != m_library.decks.end()) {
      // The server forgot our deck (a fresh database): upload it anew
      --m_pendingDecks;
      UploadDeck(*deck);
      return;
    }
    Notice("Deck \"" + ref + "\" was not accepted: " + msg.value("reason", ""));
    m_pendingDecks = 0;
    m_afterSync = nullptr;
    m_phase = Phase::Ready;
  } else if (type == "queued") {
    m_phase = Phase::Queued;
  } else if (type == "queueCancelled") {
    m_phase = Phase::Ready;
  } else if (type == "lobbyCreated") {
    m_lobbyCode = msg.at("code").get<std::string>();
    m_phase = Phase::Hosting;
  } else if (type == "matchFound") {
    m_matchId = msg.at("matchId").get<int64_t>();
    m_seed = std::stoull(msg.at("seed").get<std::string>());
    m_slot = msg.at("slot").get<int>();
    m_roundCards = {};
    m_roundsWon = {};
    m_lastRoundWinner = -1;
    m_matchWinner = -1;
    m_desyncs = 0;
    m_lobbyCode.clear();
    m_opponentConnected = true;
    m_phase = Phase::Decks;
    SendMatchDecks();
  } else if (type == "decksLocked") {
    m_phase = Phase::Waiting;
  } else if (type == "decksRejected") {
    Notice("Decks rejected: " + msg.value("reason", ""));
  } else if (type == "roundStart") {
    BeginRound(msg.at("round").get<int>(), msg.at("decks"), sim, state);
  } else if (type == "turnStart") {
    m_round = msg.at("round").get<int>();
    m_turn = msg.at("turn").get<int>();
    SetDeadline(msg.at("deadlineMs").get<int64_t>());
    m_opponentCommitted = false;
    m_planText.clear();
    m_plans = {};
    m_phase = Phase::Planning;
  } else if (type == "opponentCommitted") {
    m_opponentCommitted = true;
  } else if (type == "bothCommitted") {
    if (m_phase == Phase::Committed && !m_planText.empty())
      m_net.Send({{"type", "reveal"},
                  {"round", m_round},
                  {"turn", m_turn},
                  {"plan", m_planText},
                  {"nonce", m_nonce}});
    m_phase = Phase::Revealed;
  } else if (type == "planRejected") {
    Notice("Your plan was replaced (you stand still): " +
           msg.value("reason", ""));
  } else if (type == "turnPlans") {
    const json &plans = msg.at("plans");
    for (int s = 0; s < 2; ++s)
      DecodePlan(plans[s].get<std::string>(), s, m_plans[s]);
    m_execTick = 0;
    m_phase = Phase::Executing;
  } else if (type == "desync") {
    ++m_desyncs;
    int reference = msg.at("referenceSlot").get<int>();
    if (reference == m_slot) {
      std::string snapshot = Match::EncodeSnapshot(sim, state);
      m_net.Send({{"type", "snapshot"},
                  {"round", m_round},
                  {"turn", m_turn},
                  {"data", snapshot}});
      // Load it ourselves too: rigid bodies rebuild the same way on both
      Match::DecodeSnapshot(snapshot, sim, state);
      m_phase = Phase::Reporting;
    } else {
      m_phase = Phase::Resync;
    }
    Notice("Out of sync - resyncing from the reference player");
  } else if (type == "snapshot") {
    if (!Match::DecodeSnapshot(msg.at("data").get<std::string>(), sim, state))
      Notice("Couldn't load the resync snapshot");
    m_phase = Phase::Reporting;
  } else if (type == "roundEnd") {
    m_lastRoundWinner = msg.at("winner").get<int>();
    m_roundsWon = msg.at("roundsWon").get<std::array<int, 2>>();
    m_phase = Phase::Waiting;
  } else if (type == "matchEnd") {
    m_matchWinner = msg.at("winner").get<int>();
    m_matchEndReason = msg.value("reason", "");
    m_roundsWon = msg.at("roundsWon").get<std::array<int, 2>>();
    m_runningMatch.reset();
    m_phase = Phase::MatchOver;
  } else if (type == "opponentDisconnected") {
    m_opponentConnected = false;
    Notice("Opponent disconnected - waiting for them to come back");
  } else if (type == "opponentReconnected") {
    m_opponentConnected = true;
    Notice("Opponent is back");
  } else if (type == "catchUp") {
    CatchUp(msg, sim, state);
  } else if (type == "error") {
    Notice(msg.value("message", "server error"));
    if (m_phase == Phase::Syncing)
      m_phase = Phase::Ready;
  }
}
