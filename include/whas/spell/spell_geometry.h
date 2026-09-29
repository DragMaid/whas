#pragma once

#include "whas/spell/spell_types.h"
#include <optional>
#include <vector>

namespace SpellGeometry {

Vector2 TransformPoint(Vector2 local, Vector2 localCenter, Vector2 position,
                       float scale, float rotationDeg);

std::vector<LineSeg> TransformSegments(const std::vector<LineSeg> &local,
                                       Vector2 localCenter, Vector2 position,
                                       float scale, float rotationDeg);

bool SegmentsIntersect(const LineSeg &a, const LineSeg &b);

bool AllEndpointsInsideCircle(const std::vector<LineSeg> &segments,
                              Vector2 center, float radius);

bool SegmentsCrossAny(const std::vector<LineSeg> &candidate,
                      const std::vector<LineSeg> &existing);

// A glyph's segments where it's placed (inverted drawing if inverted)
std::vector<LineSeg> GlyphSegments(const SvgAsset &asset,
                                   const PlacedGlyph &glyph);

// Where one of a component's glyphs lands in the layered circle: shrunk to
// the component's scale, turned with it and moved to its position
PlacedGlyph ComponentGlyph(const SpellComponent &component,
                           const PlacedGlyph &glyph);

// A component's outer radius in the layered circle
inline float ComponentRadius(float scale) {
  return SPELL_OUTER_RADIUS * scale;
}

// A component must sit inside the core and clear the other components
bool IsComponentPlacementValid(const SpellComponent &component,
                               const Spell &spell,
                               std::optional<size_t> ignoreIndex =
                                   std::nullopt);

// Plain spells keep glyphs inside innerRadius; in a layered spell the glyphs
// are the outer ring's and must stay in the ring band
bool IsGlyphPlacementValid(const SvgAsset &asset, const PlacedGlyph &glyph,
                           Vector2 canvasCenter, float innerRadius,
                           const Spell &spell,
                           const std::vector<SvgAsset> &assets,
                           std::optional<size_t> ignoreIndex = std::nullopt);

float PointToSegmentDistance(Vector2 p, const LineSeg &seg);

bool HitTestGlyph(const PlacedGlyph &glyph, const SvgAsset &asset,
                  Vector2 canvasPoint, float threshold);

} // namespace SpellGeometry
