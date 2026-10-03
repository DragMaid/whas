#include "whas/spell/spell_json.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace SpellJson {

json Glyphs(const std::vector<PlacedGlyph> &glyphs) {
  json out = json::array();
  for (const PlacedGlyph &g : glyphs) {
    json j{{"assetId", g.assetId},
           {"kind", g.kind == GlyphKind::Sigil ? "sigil" : "sign"},
           {"x", g.position.x},
           {"y", g.position.y},
           {"scale", g.scale},
           {"rotation", g.rotationDeg}};
    if (g.inverted)
      j["inverted"] = true;
    out.push_back(std::move(j));
  }
  return out;
}

std::vector<PlacedGlyph> ParseGlyphs(const json &j) {
  std::vector<PlacedGlyph> out;
  if (!j.is_array())
    return out;
  for (const auto &g : j) {
    PlacedGlyph pg;
    pg.assetId = g.value("assetId", "");
    pg.kind = g.value("kind", "sign") == "sigil" ? GlyphKind::Sigil
                                                 : GlyphKind::Sign;
    pg.position.x = g.value("x", 0.0f);
    pg.position.y = g.value("y", 0.0f);
    pg.scale = g.value("scale", 1.0f);
    pg.rotationDeg = g.value("rotation", 0.0f);
    pg.inverted = g.value("inverted", false);
    if (!pg.assetId.empty())
      out.push_back(pg);
  }
  return out;
}

json Components(const std::vector<SpellComponent> &components) {
  json out = json::array();
  for (const SpellComponent &c : components)
    out.push_back({{"source", c.source},
                   {"x", c.position.x},
                   {"y", c.position.y},
                   {"scale", c.scale},
                   {"rotation", c.rotationDeg},
                   {"glyphs", Glyphs(c.glyphs)}});
  return out;
}

std::vector<SpellComponent> ParseComponents(const json &j) {
  std::vector<SpellComponent> out;
  if (!j.is_array())
    return out;
  for (const auto &c : j) {
    SpellComponent sc;
    sc.source = c.value("source", "");
    sc.position.x = c.value("x", 0.0f);
    sc.position.y = c.value("y", 0.0f);
    sc.scale = c.value("scale", 0.35f);
    sc.rotationDeg = c.value("rotation", 0.0f);
    if (c.contains("glyphs"))
      sc.glyphs = ParseGlyphs(c["glyphs"]);
    out.push_back(std::move(sc));
  }
  return out;
}

void Write(json &j, const Spell &spell) {
  j["glyphs"] = Glyphs(spell.glyphs);
  if (spell.Layered())
    j["components"] = Components(spell.components);
}

void Read(const json &j, Spell &spell) {
  spell.glyphs = j.contains("glyphs") ? ParseGlyphs(j["glyphs"])
                                      : std::vector<PlacedGlyph>{};
  spell.components = j.contains("components")
                         ? ParseComponents(j["components"])
                         : std::vector<SpellComponent>{};
}

void MigrateLegacyIds(Spell &spell, int fromFormat) {
  auto migrate = [fromFormat](std::vector<PlacedGlyph> &glyphs) {
    for (PlacedGlyph &g : glyphs) {
      if (fromFormat < 2 && g.assetId == "wind")
        g.assetId = "wind_underfoot";
      else if (fromFormat < 2 && g.assetId == "gust")
        g.assetId = "wind";
      else if (fromFormat < 3 && g.assetId == "column")
        g.assetId = "levitation";
    }
  };
  migrate(spell.glyphs);
  for (SpellComponent &c : spell.components)
    migrate(c.glyphs);
}

} // namespace SpellJson
