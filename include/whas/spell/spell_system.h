#pragma once

#include "whas/core/element.h"
#include "whas/spell/spell_shapes.h"
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
  Field,   // pulls or pushes what's in its path (pulling sign + a sigil)
  Compound, // a layered spell: fires every one of its parts at once
};


// What a guided spell (guidance sigil) chases: the nearest character (a
// human sigil beside it) or the nearest cells of an element (a second,
// smaller element sigil)
enum class HomeTarget : uint8_t { None, Human, Element };

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
  float force = 0.0f;       // Field: strength; acceleration = force/mass
  float duration = 0.0f;    // Field: seconds it lasts
  // Field: > 0 pulls toward the caster, < 0 pushes away. `element` is what
  // it moves: AIR (the wind sigil) moves everything loose.
  float pull = 0.0f;
  // Light: each mote bursts into a flash this wide (cells) that blinds for
  // this long (seconds)
  float flashRadius = 0.0f;
  float flashTime = 0.0f;

  // Guidance: particles turn toward the target at up to homeTurnRate
  // (rad/s), looking for it within homeRadius cells
  HomeTarget homeTarget = HomeTarget::None;
  Element homeElement = Element::AIR;
  float homeTurnRate = 0.0f;
  float homeRadius = 0.0f;

  // Sights set: for steerTime seconds the particles turn toward the
  // caster's cursor at up to steerRate (rad/s), then fly straight on
  float steerTime = 0.0f;
  float steerRate = 0.0f;

  // Modifier signs (element spells)
  SpellShape shape = SpellShape::Stream;
  float temperatureDelta = 0.0f; // cooling: added to what lands
  float hardnessScale = 1.0f;    // strengthening/convergence: landed hardness
  float crush = 0.0f;   // crushing: > 0 grinds what it hits to sand, < 0 reforms
  float restore = 0.0f; // repetition: resets what it hits to its natural state
  float collectRadius = 0.0f; // collection: cells around the caster...
  int collectMax = 0;         // ...and how many of them it can draw in

  // Compound: one entry per embedded spell, each ready to fire
  std::vector<SpellStats> parts;

  bool HasFlight() const;
};

struct SpellEffect {
  SpellStats stats;
  Vector2 origin{0.0f, 0.0f};
  Vector2 direction{1.0f, 0.0f};
  int emitted = 0;
  int owner = -1;             // hurtbox id of the caster
  float timeRemaining = 0.0f; // Field
  uint8_t shapePart = 0;      // which ShapeDef part is emitting
  int rows = 0;               // rows emitted so far (for the weave)
  int partRows = 0;           // rows emitted by the current part
  int bonusParticles = 0;     // drawn in by collection when cast
  int guideId = -1; // steered spells: the path its particles follow
  int castId = -1;  // on its particles, so they don't collide with each other
};

namespace SpellSystem {

constexpr float BALANCED_THRESHOLD = 0.05f;

// Sigil asset id -> what it does. Unknown ids give SpellKind::None.
SpellKind SigilKind(const std::string &assetId);
Element SigilElement(const std::string &assetId);
// Sigils that only shape a spell and need an element sigil beside them
bool IsShapeSigil(const std::string &assetId);
// Signs that can be drawn inverted
bool SignInvertible(const std::string &assetId);

SpellStats Evaluate(const Spell &spell);

// Why a spell isn't valid, in the editor's words; empty when it is
std::string Problem(const Spell &spell);

// How much an embedded spell of this scale is worth (0.5..1)
float ComponentEffectiveness(float scale);

// Rotate the aim direction by the spell's sign offset
Vector2 ResolveDirection(const SpellStats &stats, Vector2 aim);

// Caster velocity from the spell's flight (a wind spell, or the wind parts
// of a layered one); zero when it has none
Vector2 FlightVelocity(const SpellStats &stats, Vector2 aim);

// Whether a point (cells) is inside a field, and how strongly (0..1, fading
// toward the far end)
float FieldStrengthAt(const SpellEffect &field, Vector2 point);
// Whether a field moves this element
bool FieldMoves(const SpellStats &field, Element element);
// Which way a field shoves things at full strength (unit, along or against
// its direction)
Vector2 FieldPush(const SpellEffect &field);

// Emit element streams, apply fields and drop finished effects
void TickEffects(std::vector<SpellEffect> &effects, ElementContext &ctx,
                 RigidBodySystem &bodies, float dt);

} // namespace SpellSystem
