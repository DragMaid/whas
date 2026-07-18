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

bool IsGlyphPlacementValid(const SvgAsset &asset, const PlacedGlyph &glyph,
                           Vector2 canvasCenter, float innerRadius,
                           const Spell &spell,
                           const std::vector<SvgAsset> &assets,
                           std::optional<size_t> ignoreIndex = std::nullopt);

float PointToSegmentDistance(Vector2 p, const LineSeg &seg);

bool HitTestGlyph(const PlacedGlyph &glyph, const SvgAsset &asset,
                  Vector2 canvasPoint, float threshold);

} // namespace SpellGeometry
