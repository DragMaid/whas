#pragma once
#include "whas/spell/spell_types.h"
#include <vector>

// How many signs and sigils a spell uses, circle by circle, and how many
// spells a layered spell holds, against the limits ordinary matches play
// by. Spells over them still work solo and in chaos rooms, where nothing is
// counted.
namespace SpellRules {

struct Circle {
  int signs = 0;
  int sigils = 0;
};

struct Count {
  // The spell's own circle (a layered spell's outer ring) first, then each
  // component
  std::vector<Circle> circles;
  int mostSigns = 0; // in any one circle
  int signs = 0;     // in all
  int sigils = 0;

  int parts = 0;     // spells in a layered spell

  bool OverLimit() const {
    return mostSigns > SIGN_LIMIT || parts > LAYER_MAX_COMPONENTS;
  }
};

Count CountGlyphs(const Spell &spell);
// Allowed in an ordinary (non-chaos) match
inline bool WithinLimits(const Spell &spell) {
  return !CountGlyphs(spell).OverLimit();
}

} // namespace SpellRules
