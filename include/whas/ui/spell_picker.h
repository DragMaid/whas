#pragma once
#include "whas/spell/spell_types.h"
#include <imgui.h>
#include <vector>

class SpellLibrary;
class SpellThumbnails;

// Spell cards for picking spells (an enemy's, a shrine's blueprints, the
// campaign backpack): thumbnail and name, a tooltip with the glyphs and
// cast time, draggable.
namespace SpellPicker {

// Drag payload: a const Spell* to copy
constexpr const char *PAYLOAD = "WHAS_SPELL_PTR";

// A card as an invisible button (the last item afterwards, so callers can
// ask if it was clicked or hovered). Dragging it carries the spell.
void Card(const Spell &spell, ImVec2 size, SpellThumbnails &thumbs,
          bool selected = false, bool dim = false);
// What the spell is: its name, glyphs and cast time
void Tooltip(const Spell &spell);
// A spell dropped on the last item, if any
const Spell *AcceptDrop();

// The chosen spells as cards (x removes) over the library's (drag one up
// or double-click it to add; each spell at most once, matched by name).
// Returns true when `chosen` changed.
bool Draw(const char *id, std::vector<Spell> &chosen, const SpellLibrary &library,
          SpellThumbnails &thumbs);

} // namespace SpellPicker
