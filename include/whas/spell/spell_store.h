#pragma once

#include "whas/spell/spell_types.h"
#include <string>
#include <vector>

class SpellStore {
public:
  static constexpr const char *SPELLS_DIR = "data/spells";

  void EnsureDirectoryExists() const;
  std::vector<Spell> LoadAll() const;
  bool Save(const Spell &spell, std::string &errorOut) const;
  std::string SanitizeFilename(const std::string &name) const;
};
