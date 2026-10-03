#include "whas/spell/spell_store.h"
#include "whas/spell/spell_json.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void SpellStore::EnsureDirectoryExists() const {
  std::filesystem::create_directories(SPELLS_DIR);
}

std::string SpellStore::SanitizeFilename(const std::string &name) const {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
      out += c;
    else if (c == ' ')
      out += '_';
  }
  return out;
}

std::vector<Spell> SpellStore::LoadAll() const {
  std::vector<Spell> spells;
  std::filesystem::path dir(SPELLS_DIR);
  if (!std::filesystem::exists(dir))
    return spells;

  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json")
      continue;

    std::ifstream file(entry.path());
    if (!file)
      continue;

    try {
      json j;
      file >> j;
      Spell spell;
      spell.name = j.value("name", entry.path().stem().string());
      SpellJson::Read(j, spell);
      if (int format = j.value("format", 1); format < SpellJson::FORMAT)
        SpellJson::MigrateLegacyIds(spell, format);

      if (!spell.name.empty())
        spells.push_back(std::move(spell));
    } catch (...) {
      continue;
    }
  }

  std::sort(spells.begin(), spells.end(),
            [](const Spell &a, const Spell &b) { return a.name < b.name; });
  return spells;
}

bool SpellStore::Exists(const std::string &ref) const {
  return std::filesystem::exists(std::filesystem::path(SPELLS_DIR) /
                                 (ref + ".json"));
}

bool SpellStore::Remove(const std::string &ref, std::string &errorOut) const {
  std::error_code ec;
  if (!std::filesystem::remove(
          std::filesystem::path(SPELLS_DIR) / (ref + ".json"), ec)) {
    errorOut = ec ? ec.message() : "Spell file not found.";
    return false;
  }
  return true;
}

bool SpellStore::Save(const Spell &spell, std::string &errorOut) const {
  EnsureDirectoryExists();

  if (spell.name.empty()) {
    errorOut = "Spell name cannot be empty.";
    return false;
  }

  std::string filename = SanitizeFilename(spell.name);
  if (filename.empty()) {
    errorOut = "Spell name contains no valid characters.";
    return false;
  }

  json j;
  j["name"] = spell.name;
  j["format"] = SpellJson::FORMAT;
  SpellJson::Write(j, spell);

  std::filesystem::path path =
      std::filesystem::path(SPELLS_DIR) / (filename + ".json");

  std::ofstream file(path);
  if (!file) {
    errorOut = "Failed to write spell file.";
    return false;
  }

  file << j.dump(2);
  return true;
}
