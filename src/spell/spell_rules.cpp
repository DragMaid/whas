#include "whas/spell/spell_rules.h"
#include <algorithm>

namespace SpellRules {

namespace {

SpellRules::Circle CountCircle(const std::vector<PlacedGlyph> &glyphs) {
  Circle c;
  for (const PlacedGlyph &g : glyphs)
    (g.kind == GlyphKind::Sigil ? c.sigils : c.signs)++;
  return c;
}

} // namespace

Count CountGlyphs(const Spell &spell) {
  Count count;
  count.circles.push_back(CountCircle(spell.glyphs));
  for (const SpellComponent &component : spell.components)
    count.circles.push_back(CountCircle(component.glyphs));
  count.parts = static_cast<int>(spell.components.size());
  for (const Circle &c : count.circles) {
    count.mostSigns = std::max(count.mostSigns, c.signs);
    count.signs += c.signs;
    count.sigils += c.sigils;
  }
  return count;
}

} // namespace SpellRules
