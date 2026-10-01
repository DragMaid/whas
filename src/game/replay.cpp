#include "whas/game/replay.h"
#include "whas/engine/simulation.h"

using json = nlohmann::json;

bool ReplayPlayer::Load(const json &replay, std::string &error) {
  try {
    m_matchId = replay.value("matchId", int64_t{0});
    m_seed = std::stoull(replay.at("seed").get<std::string>());
    m_buildId = replay.value("buildId", "");
    if (!OptionsFromJson(replay.value("options", json()), m_options, error))
      return false;

    // Both players' six cards for each round
    std::array<std::vector<RoundCards>, 2> cards;
    for (const json &p : replay.at("players")) {
      int slot = p.at("slot").get<int>();
      if (slot < 0 || slot > 1)
        throw std::runtime_error("bad slot");
      for (const json &round : p.at("decks"))
        cards[slot].push_back(ParseRoundCards(round));
    }

    m_turns.clear();
    for (const json &t : replay.at("turns")) {
      TurnRecord rec;
      rec.round = t.at("round").get<int>();
      rec.turn = t.at("turn").get<int>();
      for (int s = 0; s < 2; ++s) {
        if (rec.round >= (int)cards[s].size())
          throw std::runtime_error("turn in a round without decks");
        const RoundCards &deck = cards[s][rec.round];
        auto resolve = [&deck](int64_t id, Spell &spell, SpellStats &stats) {
          for (const auto &card : deck)
            if (card && card->id == id) {
              spell = card->spell;
              stats = card->stats;
              return true;
            }
          return false;
        };
        std::string planError;
        json plan = json::parse(t.at("plans")[s].get<std::string>());
        if (!PlanCodec::Decode(plan, resolve, rec.plans[s], planError))
          throw std::runtime_error("plan: " + planError);
        const json &h = t.at("hashes")[s];
        if (h.is_string())
          rec.hashes[s] = std::stoull(h.get<std::string>());
      }
      m_turns.push_back(std::move(rec));
    }
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
  return true;
}

int ReplayPlayer::Turn() const {
  return Finished() ? -1 : m_turns[m_index].turn;
}

void ReplayPlayer::Start(Simulation &sim) {
  m_index = 0;
  m_tick = 0;
  m_checked = 0;
  m_mismatches = 0;
  m_report.clear();
  m_state = Match::BeginRound(sim, m_seed,
                              m_turns.empty() ? 0 : m_turns.front().round,
                              &m_options);
}

bool ReplayPlayer::Step(Simulation &sim, int ticks) {
  while (ticks-- > 0 && !Finished()) {
    const TurnRecord &t = m_turns[m_index];
    Match::ExecuteTick(sim, m_state, {&t.plans[0], &t.plans[1]}, m_tick++);
    if (m_tick == TurnController::TURN_TICKS)
      FinishTurn(sim);
  }
  return !Finished();
}

void ReplayPlayer::FinishTurn(Simulation &sim) {
  const TurnRecord &t = m_turns[m_index];
  Match::EndTurn(m_state);
  uint64_t hash = Match::Hash(sim, m_state);
  for (int s = 0; s < 2; ++s) {
    if (!t.hashes[s])
      continue;
    ++m_checked;
    if (*t.hashes[s] != hash) {
      ++m_mismatches;
      m_report.push_back("round " + std::to_string(t.round + 1) + " turn " +
                         std::to_string(t.turn + 1) + ": player " +
                         std::to_string(s) + " reported " +
                         std::to_string(*t.hashes[s]) + ", replay has " +
                         std::to_string(hash));
    }
  }
  m_tick = 0;
  ++m_index;
  // The next round starts from a fresh arena
  if (!Finished() && m_turns[m_index].round != t.round)
    m_state = Match::BeginRound(sim, m_seed, m_turns[m_index].round,
                                &m_options);
}

int ReplayPlayer::VerifyAll(Simulation &sim, std::vector<std::string> *report) {
  Start(sim);
  while (Step(sim, TurnController::TURN_TICKS))
    ;
  if (report)
    *report = m_report;
  return m_mismatches;
}
