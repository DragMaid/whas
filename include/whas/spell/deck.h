#pragma once
#include <array>
#include <string>
#include <vector>

constexpr int DECK_SLOTS = 6;   // hotbar keys 1-6
constexpr int MATCH_ROUNDS = 3; // one deck per round of a best-of-3

struct Deck {
  std::string id;
  std::string name;
  std::array<std::string, DECK_SLOTS> slots{}; // spell refs, "" = empty

  int Filled() const;
};

// Which deck the player brings to each round of a match
struct MatchDecks {
  std::array<std::string, MATCH_ROUNDS> deckIds{};
};

// All of the player's decks plus their round assignment, kept in
// data/decks/*.json and data/match_decks.json
class DeckBook {
public:
  static constexpr const char *DECKS_DIR = "data/decks";
  static constexpr const char *MATCH_FILE = "data/match_decks.json";

  // Loads everything; starts a deck from the first spells when there's none
  void Load(const std::vector<std::string> &starterRefs);

  const std::vector<Deck> &Decks() const { return m_decks; }
  const Deck *Find(const std::string &id) const;

  Deck &Create(const std::string &name);
  // Copy of a deck named "<name> copy"; returns its id
  std::string Duplicate(const std::string &id);
  void Remove(const std::string &id);
  void Rename(const std::string &id, const std::string &name);
  void SetSlot(const std::string &id, int slot, const std::string &ref);

  // Keep decks pointing at spells after a rename or delete
  void ReplaceRef(const std::string &oldRef, const std::string &newRef);
  void RemoveRef(const std::string &ref);

  const MatchDecks &Match() const { return m_match; }
  void AssignRound(int round, const std::string &deckId);

  // Deck in use outside a match (sandbox hotbar)
  const std::string &ActiveId() const { return m_activeId; }
  void SetActive(const std::string &id);

private:
  Deck *FindMutable(const std::string &id);
  std::string NewId() const;
  void SaveDeck(const Deck &deck) const;
  void SaveMatch() const;
  void FixDangling();

  std::vector<Deck> m_decks;
  MatchDecks m_match;
  std::string m_activeId;
};
