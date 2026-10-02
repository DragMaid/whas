#pragma once
#include "whas/game/match.h"
#include "whas/net/lockstep_client.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class Simulation;

// Re-simulates a stored match (GET /api/matches/{id}/replay) from its seed,
// decks and plans, checking each turn against the hashes the players
// reported. Used by the replay viewer and by `whasg --replay file --verify`.
// A turn that needed a snapshot resync can't be reproduced (snapshots
// aren't stored), so hashes after one are expected to differ.
class ReplayPlayer {
public:
  bool Load(const nlohmann::json &replay, std::string &error);

  void Start(Simulation &sim);
  // Up to `ticks` more ticks; false once the whole match has played
  bool Step(Simulation &sim, int ticks);
  // Play everything at once; returns the number of turns whose hash differed
  int VerifyAll(Simulation &sim, std::vector<std::string> *report = nullptr);

  const Match::State &State() const { return m_state; }
  int64_t MatchId() const { return m_matchId; }
  const std::string &BuildId() const { return m_buildId; }
  int TurnCount() const { return static_cast<int>(m_turns.size()); }
  int CurrentTurn() const { return m_index; } // index into the turn list
  int Round() const { return m_state.round; }
  int Turn() const;
  int Tick() const { return m_tick; }
  // Ticks in one stored turn: a planned turn, or a real-time batch
  int TicksPerRecord() const;
  bool Finished() const { return m_index >= TurnCount(); }
  int Checked() const { return m_checked; }
  int Mismatches() const { return m_mismatches; }
  const std::vector<std::string> &Report() const { return m_report; }
  // Both players' cards per round
  const std::array<std::vector<RoundCards>, 2> &Cards() const { return m_cards; }
  // Which slot saved this replay (its "local" header), -1 if unknown
  int LocalSlot() const { return m_localSlot; }

private:
  struct TurnRecord {
    int round = 0;
    int turn = 0;
    std::array<TurnPlan, 2> plans;
    std::array<std::optional<uint64_t>, 2> hashes;
  };

  void FinishTurn(Simulation &sim);

  int64_t m_matchId = 0;
  uint64_t m_seed = 0;
  MatchOptions m_options;
  std::array<std::vector<RoundCards>, 2> m_cards;
  int m_localSlot = -1;
  std::string m_buildId;
  std::vector<TurnRecord> m_turns;
  Match::State m_state;
  int m_index = 0;
  int m_tick = 0;
  int m_checked = 0;
  int m_mismatches = 0;
  std::vector<std::string> m_report;
};
