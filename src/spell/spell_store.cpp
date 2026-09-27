#include "whas/spell/spell_store.h"
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

static GlyphKind ParseKind(const std::string &s) {
  if (s == "sigil")
    return GlyphKind::Sigil;
  return GlyphKind::Sign;
}

static std::string KindToString(GlyphKind kind) {
  return kind == GlyphKind::Sigil ? "sigil" : "sign";
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

      if (j.contains("glyphs") && j["glyphs"].is_array()) {
        for (const auto &g : j["glyphs"]) {
          PlacedGlyph pg;
          pg.assetId = g.value("assetId", "");
          pg.kind = ParseKind(g.value("kind", "sign"));
          pg.position.x = g.value("x", 0.0f);
          pg.position.y = g.value("y", 0.0f);
          pg.scale = g.value("scale", 1.0f);
          pg.rotationDeg = g.value("rotation", 0.0f);
          if (!pg.assetId.empty())
            spell.glyphs.push_back(pg);
        }
      }

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
  j["glyphs"] = json::array();

  for (const auto &g : spell.glyphs) {
    j["glyphs"].push_back({{"assetId", g.assetId},
                           {"kind", KindToString(g.kind)},
                           {"x", g.position.x},
                           {"y", g.position.y},
                           {"scale", g.scale},
                           {"rotation", g.rotationDeg}});
  }

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
