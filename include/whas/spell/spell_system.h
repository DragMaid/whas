#pragma once

#include "whas/core/element.h"
#include "whas/spell/spell_types.h"
#include <raylib.h>

struct SpellProjectile {
  Vector2 position;
  Vector2 direction;
  float speed = 0.0f;
  float remainingDistance = 0.0f;
  Element outputElement = Element::WATER;
  bool active = false;
};

namespace SpellSystem {

// Extract the output element from a spell's sigils
Element GetSpellElement(const Spell &spell);

// Compute the projectile direction from mouse aim and sign glyphs
// Returns a normalized direction vector
Vector2 ComputeSpellDirection(const Spell &spell, Vector2 mouseDirection);

// Compute the projectile range (distance traveled)
float ComputeSpellRange(const Spell &spell);

// Compute the projectile speed (units per second)
float ComputeSpellSpeed(const Spell &spell);

} // namespace SpellSystem
