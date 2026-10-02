#pragma once
#include "whas/net/lockstep_client.h"
#include <array>
#include <string>
#include <vector>

class UI;

// The spells both players brought to a match, round by round, with buttons
// to copy a spell or a whole round's deck into your own library. Shown when
// an online match ends and beside a replay.
class GrimoirePanel {
public:
  explicit GrimoirePanel(UI &ui) : m_ui(ui) {}

  // cards: per slot, per round. localSlot is yours (-1: unknown, both
  // players are shown as players 1 and 2); the opponent is shown first.
  void Draw(const std::array<std::vector<RoundCards>, 2> &cards, int localSlot);
  // Next time starts on the opponent again
  void Reset() {
    m_shown = -1;
    m_status.clear();
  }

private:
  void DrawRound(const RoundCards &cards, int slot, int round);
  bool CopySpell(const Spell &spell, std::string &ref);
  void CopyDeck(const RoundCards &cards, int round);

  UI &m_ui;
  int m_shown = -1; // slot on show
  std::string m_status;
};
