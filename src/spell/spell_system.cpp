#include "whas/spell/spell_system.h"
#include <cmath>
#include <algorithm>

Element SpellSystem::GetSpellElement(const Spell &spell) {
  // Find the first sigil in the spell; default all to WATER for now
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind == GlyphKind::Sigil) {
      return Element::WATER;
    }
  }
  return Element::WATER;
}

Vector2 SpellSystem::ComputeSpellDirection(const Spell &spell,
                                          Vector2 mouseDirection) {
  // Compute sign bias: sum of (scale * rotationVector) for all signs
  Vector2 signBias{0.0f, 0.0f};
  
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind != GlyphKind::Sign) continue;
    
    // Convert rotation in degrees to a unit direction vector
    float radians = glyph.rotationDeg * (3.14159265f / 180.0f);
    Vector2 signDir{std::cos(radians), std::sin(radians)};
    
    // Add weighted by scale to the bias
    signBias.x += signDir.x * glyph.scale;
    signBias.y += signDir.y * glyph.scale;
  }
  
  // Calculate the magnitude of signBias
  float biasMagnitude = std::sqrt(signBias.x * signBias.x + signBias.y * signBias.y);
  
  // If bias is near zero, use mouse direction as-is (signs cancel out)
  if (biasMagnitude < 0.1f) {
    return mouseDirection;
  }
  
  // Otherwise blend mouseDir + signBias and normalize
  float signInfluence = 0.5f; // Adjust to change how much signs affect direction
  Vector2 blended{
    mouseDirection.x + signBias.x * signInfluence,
    mouseDirection.y + signBias.y * signInfluence
  };
  
  float blendedMag = std::sqrt(blended.x * blended.x + blended.y * blended.y);
  if (blendedMag < 0.001f) {
    return mouseDirection;
  }
  
  return {blended.x / blendedMag, blended.y / blendedMag};
}

float SpellSystem::ComputeSpellRange(const Spell &spell) {
  // Base range: ~200 pixels
  float baseRange = 200.0f;
  
  int signCount = 0;
  float totalScale = 0.0f;
  
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind == GlyphKind::Sign) {
      signCount++;
      totalScale += glyph.scale;
    }
  }
  
  if (signCount == 0) {
    return baseRange;
  }
  
  // Range increases with number of signs and their scale
  float avgScale = totalScale / signCount;
  return baseRange + (signCount * 50.0f) + (totalScale * 30.0f);
}

float SpellSystem::ComputeSpellSpeed(const Spell &spell) {
  // Base speed: ~100 pixels per second
  float baseSpeed = 100.0f;
  
  float totalScale = 0.0f;
  int signCount = 0;
  
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind == GlyphKind::Sign) {
      signCount++;
      totalScale += glyph.scale;
    }
  }
  
  if (signCount == 0) {
    return baseSpeed;
  }
  
  // Speed increases with average sign scale
  float avgScale = totalScale / signCount;
  return baseSpeed + (avgScale * 50.0f);
}
