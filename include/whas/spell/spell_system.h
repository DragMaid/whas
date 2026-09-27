#pragma once

#include "whas/core/element.h"
#include "whas/spell/spell_types.h"
#include <raylib.h>
#include <string>
#include <vector>

struct ElementContext;
class RigidBodySystem;

// What the sigil makes the spell do
enum class SpellKind : uint8_t {
  None,    // no usable sigil
  Element, // fires a stream of an element
  Flight,  // launches the caster along the spell direction (wind sigil)
  Gust,    // a push field that sends everything in it flying (gust sigil)
};

// Everything a spell does, derived from its glyphs.
//
// Signs are vectors in the circle's own frame, where "up" (0, -1) is the aim
// direction. A balanced circle (net vector ~0) fires straight at the aim; any
// leftover net vector bends the spell sideways and wastes some of its force.
struct SpellStats {
  bool valid = false; // exactly one sigil with a known effect
  SpellKind kind = SpellKind::None;
  Element element = Element::AIR; // for SpellKind::Element

  Vector2 netLocal{0.0f, 0.0f}; // sum of sign vectors, circle frame
  float totalMagnitude = 0.0f;  // sum of sign scales
  float imbalance = 0.0f;       // |netLocal| / totalMagnitude, 0..1
  float offsetRad = 0.0f;       // signed deviation from aim (+ = clockwise)

  float speed = 0.0f;    // cells per second
  float range = 0.0f;    // cells travelled before the spell lets go
  float density = 0.0f;  // mass per particle, from sigil scale
  float power = 0.0f;    // penetration budget per particle
  float diameter = 1.0f; // beam width in cells
  int particleCount = 0;
  float temperature = 0.0f; // heat of what lands (fire spells), 0 = default

  float launchSpeed = 0.0f; // Flight: caster velocity, cells/s
  float force = 0.0f;       // Gust: push strength; acceleration = force/mass
  float duration = 0.0f;    // Gust: seconds the field lasts
};

struct SpellEffect {
  SpellStats stats;
  Vector2 origin{0.0f, 0.0f};
  Vector2 direction{1.0f, 0.0f};
  int emitted = 0;
  int owner = -1;             // hurtbox id of the caster
  float timeRemaining = 0.0f; // Gust
};

namespace SpellSystem {

constexpr float BALANCED_THRESHOLD = 0.05f;

// Sigil asset id -> what it does. Unknown ids give SpellKind::None.
SpellKind SigilKind(const std::string &assetId);
Element SigilElement(const std::string &assetId);

SpellStats Evaluate(const Spell &spell);

// Rotate the aim direction by the spell's sign offset
Vector2 ResolveDirection(const SpellStats &stats, Vector2 aim);

// Whether a point (cells) is inside a gust's field, and how strongly (0..1,
// fading toward the far end)
float GustStrengthAt(const SpellEffect &gust, Vector2 point);

// Emit element streams, apply gust fields and drop finished effects
void TickEffects(std::vector<SpellEffect> &effects, ElementContext &ctx,
                 RigidBodySystem &bodies, float dt);

} // namespace SpellSystem
