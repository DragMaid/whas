#pragma once

#include "whas/spell/spell_types.h"
#include <string>
#include <vector>

class SpellStore {
public:
  static constexpr const char *SPELLS_DIR = "data/spells";
  // Another folder of spells (a campaign's backpack)
  void SetDirectory(std::string dir) { m_dir = std::move(dir); }

  void EnsureDirectoryExists() const;
  std::vector<Spell> LoadAll() const;
  bool Save(const Spell &spell, std::string &errorOut) const;
  // Delete the file for a spell reference (see SpellLibrary::RefOf)
  bool Remove(const std::string &ref, std::string &errorOut) const;
  bool Exists(const std::string &ref) const;
  std::string SanitizeFilename(const std::string &name) const;

private:
  std::string m_dir = SPELLS_DIR;
};
