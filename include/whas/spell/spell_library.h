#pragma once
#include "whas/spell/spell_store.h"
#include "whas/spell/spell_types.h"
#include <cstdint>
#include <string>
#include <vector>

// The player's saved spells, shared by the editor, the hotbar and the decks.
// A spell is referred to by its file name (its sanitized name), which decks
// store; renames and deletes are reported so decks can follow.
class SpellLibrary {
public:
  void Load();
  // Keep the spells in another folder (call before Load)
  void SetDirectory(std::string dir) { m_store.SetDirectory(std::move(dir)); }

  const std::vector<Spell> &All() const { return m_spells; }
  const Spell *Find(const std::string &ref) const;
  std::string RefOf(const Spell &spell) const;
  std::string RefOf(const std::string &name) const;

  // Add or overwrite (by name)
  bool Save(const Spell &spell, std::string &error);
  bool Remove(const std::string &ref, std::string &error);
  // On success newRef is the renamed spell's reference
  bool Rename(const std::string &ref, const std::string &newName,
              std::string &newRef, std::string &error);
  // Saves "<name> copy" (or "copy 2", ...) and returns its reference
  bool Duplicate(const std::string &ref, std::string &newRef,
                 std::string &error);

  // A spell from someone else (an opponent's deck). One you already have
  // with the same drawing is reused; otherwise it's saved under its own
  // name, or "<name> (theirs)" when that name is taken. ref is the result.
  bool Import(const Spell &spell, std::string &ref, std::string &error);
  // The library's spell drawn exactly like this one, if any
  const Spell *FindSameDrawing(const Spell &spell) const;
  // Same glyphs and components, whatever the names
  static bool SameDrawing(const Spell &a, const Spell &b);

  // Bumped on every change so views can refresh
  uint32_t Version() const { return m_version; }

private:
  SpellStore m_store;
  std::vector<Spell> m_spells;
  uint32_t m_version = 0;
};
