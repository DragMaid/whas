#include "whas/spell/spell_system.h"
#include "whas/element/base/econtext.h"
#include "whas/physics/particle_system.h"
#include <algorithm>
#include <cmath>

namespace {

bool ContainsCell(const std::vector<Vector2> &cells, int x, int y) {
  return std::any_of(cells.begin(), cells.end(), [&](const Vector2 &cell) {
    return static_cast<int>(cell.x) == x && static_cast<int>(cell.y) == y;
  });
}

} // namespace

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
    float radians = glyph.rotationDeg * (3.14f / 180.0f);
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

float SpellSystem::ComputeSignBiasMagnitude(const Spell &spell) {
  Vector2 signBias{0.0f, 0.0f};
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind != GlyphKind::Sign) continue;
    float radians = glyph.rotationDeg * (3.14159265f / 180.0f);
    Vector2 signDir{std::cos(radians), std::sin(radians)};
    signBias.x += signDir.x * glyph.scale;
    signBias.y += signDir.y * glyph.scale;
  }
  return std::sqrt(signBias.x * signBias.x + signBias.y * signBias.y);
}

int SpellSystem::ComputeSpellWaveCount(const Spell &spell) {
  int particleCount = ComputeSpellParticleCount(spell);
  float diameter = std::max(1.0f, ComputeSpellDiameter(spell));
  return std::max(1, static_cast<int>(std::ceil(particleCount / diameter)));
}

int SpellSystem::ComputeSpellParticleCount(const Spell &spell) {
  int signCount = 0;
  float totalScale = 0.0f;
  for (const auto &glyph : spell.glyphs) {
    if (glyph.kind == GlyphKind::Sign) {
      signCount++;
      totalScale += glyph.scale;
    }
  }

  int baseCount = 12;
  int count = baseCount + signCount * 4 + static_cast<int>(std::round(totalScale * 6.0f));
  return std::max(1, std::min(60, count));
}

float SpellSystem::ComputeSpellDiameter(const Spell &spell) {
  return std::max(1.0f, spell.diameter);
}

std::vector<Vector2> SpellSystem::ComputeSpellWavePositions(
    const Spell &spell, Vector2 origin, Vector2 direction, float range,
    float diameter, int waveCount, int particlesPerWave) {
  std::vector<Vector2> cells;
  if (range <= 0.0f)
    return cells;

  float diameterCells = std::max(1.0f, diameter);
  int radius = static_cast<int>(std::ceil(diameterCells * 0.5f));
  int stepCount = std::max(1, std::max(waveCount, particlesPerWave));
  float stepSize = std::max(1.0f, range / static_cast<float>(stepCount));

  for (int wave = 0; wave < waveCount; ++wave) {
    for (int particle = 0; particle < particlesPerWave; ++particle) {
      float progress = (wave * particlesPerWave + particle + 1) /
                       static_cast<float>(waveCount * particlesPerWave);
      progress = std::min(1.0f, progress);
      Vector2 sample{origin.x + direction.x * range * progress,
                     origin.y + direction.y * range * progress};

      int cx = static_cast<int>(std::floor(sample.x));
      int cy = static_cast<int>(std::floor(sample.y));

      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          if (dx * dx + dy * dy > radius * radius)
            continue;
          int x = cx + dx;
          int y = cy + dy;
          if (ContainsCell(cells, x, y))
            continue;
          cells.push_back({static_cast<float>(x), static_cast<float>(y)});
        }
      }

      if (stepSize > 1.0f) {
        for (int i = 1; i < static_cast<int>(stepSize); ++i) {
          Vector2 follow{origin.x + direction.x * range * (progress + i / stepSize),
                         origin.y + direction.y * range * (progress + i / stepSize)};
          int fx = static_cast<int>(std::floor(follow.x));
          int fy = static_cast<int>(std::floor(follow.y));
          for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
              if (dx * dx + dy * dy > radius * radius)
                continue;
              int x = fx + dx;
              int y = fy + dy;
              if (ContainsCell(cells, x, y))
                continue;
              cells.push_back({static_cast<float>(x), static_cast<float>(y)});
            }
          }
        }
      }
    }
  }

  return cells;
}

void SpellSystem::ApplySpellEffectToParticle(Particle &particle,
                                             SpellEffect &effect,
                                             const ElementContext &ctx) {
  // TODO: not sure what the intentionally not used is for
  (void)ctx;
  if (!particle.active)
    return;

  Element outputElement = GetSpellElement(effect.spell);
  effect.hadTargetInZone = true;
  particle.element = outputElement;
  particle.vel.x *= 0.95f;
  particle.vel.y *= 0.95f;
  particle.spellActive = false;
}
