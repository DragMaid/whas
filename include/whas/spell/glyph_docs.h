#pragma once
#include "whas/spell/spell_types.h"
#include <string>

// What each sign and sigil is called and what it does, for the editor's
// tooltips and panels. Kept next to the rules in spell_system.cpp: change
// one, change the other.
namespace GlyphDocs {

struct Info {
  const char *name; // shown instead of the asset id
  const char *text; // what it does, how size and inverting change it
};

// For an unknown glyph the name is null and the text empty
Info Get(const std::string &assetId);

} // namespace GlyphDocs
