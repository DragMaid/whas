#include "whas/spell/deck.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

int Deck::Filled() const {
  return static_cast<int>(std::count_if(slots.begin(), slots.end(),
                                        [](auto &s) { return !s.empty(); }));
}

void DeckBook::Load(const std::vector<std::string> &starterRefs) {
  m_decks.clear();
  fs::create_directories(DECKS_DIR);
  for (const auto &entry : fs::directory_iterator(DECKS_DIR)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
      continue;
    try {
      std::ifstream file(entry.path());
      json j;
      file >> j;
      Deck deck;
      deck.id = j.value("id", entry.path().stem().string());
      deck.name = j.value("name", deck.id);
      auto slots = j.value("slots", std::vector<std::string>{});
      for (int i = 0; i < DECK_SLOTS && i < (int)slots.size(); ++i)
        deck.slots[i] = slots[i];
      m_decks.push_back(std::move(deck));
    } catch (...) {
      continue;
    }
  }
  std::sort(m_decks.begin(), m_decks.end(),
            [](const Deck &a, const Deck &b) { return a.name < b.name; });

  if (m_decks.empty()) {
    Deck &starter = Create("Starter");
    for (int i = 0; i < DECK_SLOTS && i < (int)starterRefs.size(); ++i)
      starter.slots[i] = starterRefs[i];
    SaveDeck(starter);
  }

  try {
    std::ifstream file(MATCH_FILE);
    if (file) {
      json j;
      file >> j;
      auto ids = j.value("rounds", std::vector<std::string>{});
      for (int i = 0; i < MATCH_ROUNDS && i < (int)ids.size(); ++i)
        m_match.deckIds[i] = ids[i];
      m_activeId = j.value("active", "");
    }
  } catch (...) {
  }
  FixDangling();
}

const Deck *DeckBook::Find(const std::string &id) const {
  for (const Deck &d : m_decks)
    if (d.id == id)
      return &d;
  return nullptr;
}

Deck *DeckBook::FindMutable(const std::string &id) {
  return const_cast<Deck *>(Find(id));
}

std::string DeckBook::NewId() const {
  for (int n = 1;; ++n) {
    std::string id = "deck" + std::to_string(n);
    if (!Find(id) && !fs::exists(fs::path(DECKS_DIR) / (id + ".json")))
      return id;
  }
}

Deck &DeckBook::Create(const std::string &name) {
  Deck deck;
  deck.id = NewId();
  deck.name = name;
  m_decks.push_back(deck);
  SaveDeck(m_decks.back());
  if (m_activeId.empty())
    SetActive(deck.id);
  return m_decks.back();
}

std::string DeckBook::Duplicate(const std::string &id) {
  const Deck *src = Find(id);
  if (!src)
    return {};
  Deck copy = *src;
  copy.id = NewId();
  copy.name = src->name + " copy";
  m_decks.push_back(copy);
  SaveDeck(copy);
  return copy.id;
}

void DeckBook::Remove(const std::string &id) {
  std::error_code ec;
  fs::remove(fs::path(DECKS_DIR) / (id + ".json"), ec);
  m_decks.erase(std::remove_if(m_decks.begin(), m_decks.end(),
                               [&](const Deck &d) { return d.id == id; }),
                m_decks.end());
  if (m_decks.empty())
    Create("Deck");
  FixDangling();
}

void DeckBook::Rename(const std::string &id, const std::string &name) {
  if (Deck *d = FindMutable(id)) {
    d->name = name;
    SaveDeck(*d);
  }
}

void DeckBook::SetSlot(const std::string &id, int slot,
                       const std::string &ref) {
  Deck *d = FindMutable(id);
  if (!d || slot < 0 || slot >= DECK_SLOTS)
    return;
  d->slots[slot] = ref;
  SaveDeck(*d);
}

void DeckBook::ReplaceRef(const std::string &oldRef,
                          const std::string &newRef) {
  for (Deck &d : m_decks) {
    bool changed = false;
    for (auto &s : d.slots)
      if (s == oldRef) {
        s = newRef;
        changed = true;
      }
    if (changed)
      SaveDeck(d);
  }
}

void DeckBook::RemoveRef(const std::string &ref) { ReplaceRef(ref, ""); }

void DeckBook::AssignRound(int round, const std::string &deckId) {
  if (round < 0 || round >= MATCH_ROUNDS)
    return;
  m_match.deckIds[round] = deckId;
  SaveMatch();
}

void DeckBook::SetActive(const std::string &id) {
  m_activeId = id;
  SaveMatch();
}

void DeckBook::SaveDeck(const Deck &deck) const {
  fs::create_directories(DECKS_DIR);
  json j{{"id", deck.id},
         {"name", deck.name},
         {"slots", std::vector<std::string>(deck.slots.begin(),
                                            deck.slots.end())}};
  std::ofstream(fs::path(DECKS_DIR) / (deck.id + ".json")) << j.dump(2);
}

void DeckBook::SaveMatch() const {
  json j{{"rounds", std::vector<std::string>(m_match.deckIds.begin(),
                                             m_match.deckIds.end())},
         {"active", m_activeId}};
  std::ofstream(MATCH_FILE) << j.dump(2);
}

// Point the active deck and round assignments at decks that exist
void DeckBook::FixDangling() {
  const std::string &first = m_decks.front().id;
  bool changed = false;
  if (!Find(m_activeId)) {
    m_activeId = first;
    changed = true;
  }
  for (auto &id : m_match.deckIds)
    if (!Find(id)) {
      id = m_activeId;
      changed = true;
    }
  if (changed)
    SaveMatch();
}
