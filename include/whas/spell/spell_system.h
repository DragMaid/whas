#pragma once

#include "whas/core/element.h"
#include "whas/spell/spell_types.h"
#include <raylib.h>
#include <vector>

struct Particle;
struct ElementContext;

struct SpellProjectile {
  Vector2 position;
  Vector2 direction;
  float speed = 0.0f;
  float remainingDistance = 0.0f;
  Element outputElement = Element::WATER;
  bool active = false;
};

struct SpellEffect {
  Spell spell;
  Vector2 origin{0.0f, 0.0f};
  Vector2 direction{1.0f, 0.0f};
  float range = 0.0f;
  float speed = 0.0f;
  float diameter = 5.0f;
  float waveSpacing = 1.0f;
  std::vector<Vector2> affectedCells;
  std::vector<Element> targetedElements;
  Color color = WHITE;
  bool active = true;
  bool expired = false;
  bool hadTargetInZone = false;
  int waveIndex = 0;
  int particleCount = 0;
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

// Returns the magnitude of the directional bias contributed by sign glyphs
// A small magnitude (~<0.1) indicates the signs effectively cancel out
float ComputeSignBiasMagnitude(const Spell &spell);

// Number of waves to emit for a spell effect
int ComputeSpellWaveCount(const Spell &spell);

// Number of particles to emit per wave
int ComputeSpellParticleCount(const Spell &spell);

// Diameter in cells for the spell affect area
float ComputeSpellDiameter(const Spell &spell);

// Compute the cell-space spawn positions for the spell effect waves
std::vector<Vector2> ComputeSpellWavePositions(const Spell &spell, Vector2 origin,
                                              Vector2 direction, float range,
                                              float diameter, int waveCount,
                                              int particlesPerWave);

// Apply the spell effect to a particle if it is inside the active zone
void ApplySpellEffectToParticle(Particle &particle, SpellEffect &effect,
                                const ElementContext &ctx);

} // namespace SpellSystem
