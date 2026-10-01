#pragma once
#include "whas/game/match.h"
#include "whas/net/net_client.h"
#include "whas/net/plan_codec.h"
#include "whas/spell/deck.h"
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

class Simulation;

// A spell as the server hands it out for a match: its id, the glyphs (for
// drawing) and the authoritative stats
struct MatchCard {
  int64_t id = 0;
  Spell spell;
  SpellStats stats;
};
using RoundCards = std::array<std::optional<MatchCard>, DECK_SLOTS>;

// A round's six cards from the server's JSON (null entries = empty slots)
RoundCards ParseRoundCards(const nlohmann::json &cards);

// The client side of the online protocol (docs/protocol.md), without any
// drawing or input: the game UI and headless bots both drive it.
//
// Per turn: turnStart -> the owner plans and calls SubmitPlan -> commit ->
// bothCommitted -> reveal -> turnPlans -> the owner steps the execution
// (StepExecution) -> stateHash. It keeps the match seed, both players' round
// decks and every plan so it can rebuild the match after a reconnect.
class LockstepClient {
public:
  enum class Phase {
    Offline,    // not connected
    Connecting, // socket opening / hello sent
    Ready,      // connected, in no match
    Syncing,    // uploading spells and decks before queueing
    Queued,
    Hosting,  // waiting in a lobby (LobbyCode())
    Decks,    // match found, round decks being locked
    Waiting,  // between turns / rounds
    Planning, // turnStart received: plan and SubmitPlan before the deadline
    Committed,
    Revealed,
    Executing, // plans in: step the turn with StepExecution
    Reporting, // hash sent
    Resync,    // waiting for the reference snapshot
    MatchOver,
  };

  // Spells and decks as the local files have them, uploaded before a match
  struct LibrarySpell {
    std::string ref;
    Spell spell;
  };
  struct Library {
    std::vector<LibrarySpell> spells;
    std::vector<Deck> decks;
    MatchDecks match;
  };

  explicit LockstepClient(std::string buildId = WHAS_SIM_BUILD_ID);
  ~LockstepClient();
  LockstepClient(const LockstepClient &) = delete;
  LockstepClient &operator=(const LockstepClient &) = delete;

  // Tokens and server deck ids are kept per server in this file
  void SetIdentityFile(const std::string &path) {
    m_identityFile = path;
    m_identityClaimed = true; // chosen explicitly (--identity)
  }
  const std::string &IdentityFile() const { return m_identityFile; }
  void Connect(const std::string &url);
  void Disconnect();
  const std::string &Url() const { return m_url; }

  void SetLibrary(Library library) { m_library = std::move(library); }
  void QuickMatch();
  // The room plays by these options (map pool, modes)
  void CreateLobby(const MatchOptions &options = {});
  void JoinLobby(const std::string &code);
  void Rejoin();
  void CancelWaiting();
  void Leave(); // forfeits a running match

  // Handle network traffic. May rebuild the world (round start, catch-up,
  // resync), so it takes the match's simulation and state.
  void Update(Simulation &sim, Match::State &state);

  void SubmitPlan(const TurnPlan &plan);
  // Run up to `ticks` ticks of the current turn; true once the turn is done
  // and its hash reported
  bool StepExecution(Simulation &sim, Match::State &state, int ticks);

  Phase GetPhase() const { return m_phase; }
  bool InMatch() const;
  int Slot() const { return m_slot; }
  int Round() const { return m_round; }
  int Turn() const { return m_turn; }
  int ExecutedTicks() const { return m_execTick; }
  uint64_t Seed() const { return m_seed; }
  const MatchOptions &Options() const { return m_options; }
  int64_t MatchId() const { return m_matchId; }
  int64_t PlayerId() const { return m_playerId; }
  const std::string &Token() const { return m_token; }
  std::optional<int64_t> RunningMatch() const { return m_runningMatch; }
  const std::string &LobbyCode() const { return m_lobbyCode; }
  // Seconds left to commit (Planning) or -1
  float SecondsLeft() const;
  bool OpponentCommitted() const { return m_opponentCommitted; }
  bool OpponentConnected() const { return m_opponentConnected; }
  const RoundCards &Cards(int slot) const { return m_cards[slot]; }
  const std::array<TurnPlan, 2> &Plans() const { return m_plans; }
  const std::array<int, 2> &RoundsWon() const { return m_roundsWon; }
  int LastRoundWinner() const { return m_lastRoundWinner; }
  int RoundEnds() const { return m_roundEnds; } // bumps on every roundEnd
  int MatchWinner() const { return m_matchWinner; }
  const std::string &MatchEndReason() const { return m_matchEndReason; }
  int Desyncs() const { return m_desyncs; }

  // Things worth telling the player ("opponent disconnected", errors...)
  std::vector<std::string> TakeNotices();

private:
  void Handle(const nlohmann::json &msg, Simulation &sim, Match::State &state);
  void SayHello();
  void StartSync(const char *then);
  void ContinueSync();
  void UploadDeck(const Deck &deck);
  void SendMatchDecks();
  void BeginRound(int round, const nlohmann::json &decks, Simulation &sim,
                  Match::State &state);
  bool DecodePlan(const std::string &text, int slot, TurnPlan &plan);
  void ReportHash(Simulation &sim, Match::State &state);
  void ReadOptions(const nlohmann::json &msg);
  void CatchUp(const nlohmann::json &msg, Simulation &sim, Match::State &state);
  void Notice(std::string text);
  void LoadIdentity();
  void SaveIdentity() const;
  void SetDeadline(int64_t ms);

  NetClient m_net;
  std::string m_buildId;
  std::string m_url;
  std::string m_identityFile = "data/guest.json";
  // Each running copy of the game needs its own guest: two windows sharing
  // one token are the same player, and nobody is paired with themselves.
  // The first free data/guest[-N].json is locked for as long as we run.
  void ClaimIdentityFile();
  bool m_identityClaimed = false;
  int m_identityLock = -1;
  std::string m_token;
  std::map<std::string, int64_t> m_serverDecks; // local deck id -> server id
  int64_t m_playerId = 0;
  std::optional<int64_t> m_runningMatch;

  Library m_library;
  std::map<std::string, int64_t> m_spellIds; // local ref -> server id
  int m_pendingUploads = 0;
  int m_pendingDecks = 0;
  enum class SyncStage { Spells, Decks } m_syncStage = SyncStage::Spells;
  const char *m_afterSync = nullptr; // "queue" / "lobby" / join code
  std::string m_joinCode;

  Phase m_phase = Phase::Offline;
  std::string m_lobbyCode;
  int64_t m_matchId = 0;
  uint64_t m_seed = 0;
  MatchOptions m_options;      // of the running match
  MatchOptions m_lobbyOptions; // sent with createLobby
  int m_slot = -1;
  int m_round = 0;
  int m_turn = 0;
  std::chrono::steady_clock::time_point m_deadline{};
  bool m_opponentCommitted = false;
  bool m_opponentConnected = true;

  // Both players' cards per round, filled as rounds start
  std::array<std::vector<RoundCards>, 2> m_roundCards;
  std::array<RoundCards, 2> m_cards{};
  std::string m_planText; // our committed plan, kept for the reveal
  std::string m_nonce;
  std::array<TurnPlan, 2> m_plans;
  int m_execTick = 0;
  std::array<int, 2> m_roundsWon{};
  int m_lastRoundWinner = -1;
  int m_roundEnds = 0;
  int m_matchWinner = -1;
  std::string m_matchEndReason;
  int m_desyncs = 0;
  std::vector<std::string> m_notices;
};
