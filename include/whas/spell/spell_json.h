#pragma once
#include "whas/spell/spell_types.h"
#include <nlohmann/json_fwd.hpp>
#include <vector>

// Glyphs and embedded spells as spell files and the server store them.
// Optional fields ("inverted", "components") are left out when unset, so
// plain spells keep exactly their old form (and their server hash).
namespace SpellJson {

nlohmann::json Glyphs(const std::vector<PlacedGlyph> &glyphs);
// Missing fields take their defaults; glyphs without an asset are dropped
std::vector<PlacedGlyph> ParseGlyphs(const nlohmann::json &j);

nlohmann::json Components(const std::vector<SpellComponent> &components);
std::vector<SpellComponent> ParseComponents(const nlohmann::json &j);

// The glyphs and components of a spell into / out of an object with
// "glyphs" and (if layered) "components" keys
void Write(nlohmann::json &j, const Spell &spell);
void Read(const nlohmann::json &j, Spell &spell);

} // namespace SpellJson
