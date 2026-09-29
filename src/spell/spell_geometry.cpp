#include "whas/spell/spell_geometry.h"
#include <algorithm>
#include <cmath>

namespace SpellGeometry {

static constexpr float EPS = 1e-4f;

Vector2 TransformPoint(Vector2 local, Vector2 localCenter, Vector2 position,
                       float scale, float rotationDeg) {
  float rad = rotationDeg * DEG2RAD;
  float c = std::cos(rad);
  float s = std::sin(rad);
  Vector2 scaled{(local.x - localCenter.x) * scale,
                 (local.y - localCenter.y) * scale};
  Vector2 rotated{scaled.x * c - scaled.y * s, scaled.x * s + scaled.y * c};
  return {position.x + rotated.x, position.y + rotated.y};
}

std::vector<LineSeg> TransformSegments(const std::vector<LineSeg> &local,
                                       Vector2 localCenter, Vector2 position,
                                       float scale, float rotationDeg) {
  std::vector<LineSeg> out;
  out.reserve(local.size());
  for (const auto &seg : local) {
    out.push_back({TransformPoint(seg.a, localCenter, position, scale,
                                  rotationDeg),
                   TransformPoint(seg.b, localCenter, position, scale,
                                  rotationDeg)});
  }
  return out;
}

static float Cross(Vector2 a, Vector2 b, Vector2 c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool OnSegment(Vector2 a, Vector2 b, Vector2 p) {
  return p.x >= std::min(a.x, b.x) - EPS && p.x <= std::max(a.x, b.x) + EPS &&
         p.y >= std::min(a.y, b.y) - EPS && p.y <= std::max(a.y, b.y) + EPS;
}

bool SegmentsIntersect(const LineSeg &a, const LineSeg &b) {
  float d1 = Cross(a.a, a.b, b.a);
  float d2 = Cross(a.a, a.b, b.b);
  float d3 = Cross(b.a, b.b, a.a);
  float d4 = Cross(b.a, b.b, a.b);

  if (((d1 > EPS && d2 < -EPS) || (d1 < -EPS && d2 > EPS)) &&
      ((d3 > EPS && d4 < -EPS) || (d3 < -EPS && d4 > EPS)))
    return true;

  if (std::fabs(d1) <= EPS && OnSegment(a.a, a.b, b.a))
    return true;
  if (std::fabs(d2) <= EPS && OnSegment(a.a, a.b, b.b))
    return true;
  if (std::fabs(d3) <= EPS && OnSegment(b.a, b.b, a.a))
    return true;
  if (std::fabs(d4) <= EPS && OnSegment(b.a, b.b, a.b))
    return true;

  return false;
}

bool AllEndpointsInsideCircle(const std::vector<LineSeg> &segments,
                              Vector2 center, float radius) {
  float r2 = radius * radius;
  for (const auto &seg : segments) {
    float da = (seg.a.x - center.x) * (seg.a.x - center.x) +
               (seg.a.y - center.y) * (seg.a.y - center.y);
    float db = (seg.b.x - center.x) * (seg.b.x - center.x) +
               (seg.b.y - center.y) * (seg.b.y - center.y);
    if (da > r2 + EPS || db > r2 + EPS)
      return false;
  }
  return true;
}

bool SegmentsCrossAny(const std::vector<LineSeg> &candidate,
                      const std::vector<LineSeg> &existing) {
  for (const auto &c : candidate) {
    for (const auto &e : existing) {
      if (SegmentsIntersect(c, e))
        return true;
    }
  }
  return false;
}

std::vector<LineSeg> GlyphSegments(const SvgAsset &asset,
                                   const PlacedGlyph &glyph) {
  return TransformSegments(asset.SegmentsFor(glyph.inverted),
                           asset.localCenter, glyph.position, glyph.scale,
                           glyph.rotationDeg);
}

PlacedGlyph ComponentGlyph(const SpellComponent &component,
                           const PlacedGlyph &glyph) {
  PlacedGlyph out = glyph;
  out.position = TransformPoint(glyph.position, {0, 0}, component.position,
                                component.scale, component.rotationDeg);
  out.scale = glyph.scale * component.scale;
  out.rotationDeg = glyph.rotationDeg + component.rotationDeg;
  return out;
}

bool IsComponentPlacementValid(const SpellComponent &component,
                               const Spell &spell,
                               std::optional<size_t> ignoreIndex) {
  float r = ComponentRadius(component.scale);
  if (std::hypot(component.position.x, component.position.y) + r >
      LAYER_CORE_RADIUS + EPS)
    return false;
  for (size_t i = 0; i < spell.components.size(); ++i) {
    if (ignoreIndex && *ignoreIndex == i)
      continue;
    const SpellComponent &other = spell.components[i];
    float d = std::hypot(component.position.x - other.position.x,
                         component.position.y - other.position.y);
    if (d < r + ComponentRadius(other.scale) - EPS)
      return false;
  }
  return true;
}

bool IsGlyphPlacementValid(const SvgAsset &asset, const PlacedGlyph &glyph,
                           Vector2 canvasCenter, float innerRadius,
                           const Spell &spell,
                           const std::vector<SvgAsset> &assets,
                           std::optional<size_t> ignoreIndex) {
  auto world = GlyphSegments(asset, glyph);

  if (spell.Layered()) {
    if (!AllEndpointsInsideCircle(world, canvasCenter, LAYER_RING_OUTER))
      return false;
    for (const LineSeg &seg : world)
      if (PointToSegmentDistance(canvasCenter, seg) < LAYER_RING_INNER - EPS)
        return false;
  } else if (!AllEndpointsInsideCircle(world, canvasCenter, innerRadius)) {
    return false;
  }

  for (size_t i = 0; i < spell.glyphs.size(); ++i) {
    if (ignoreIndex && *ignoreIndex == i)
      continue;

    const PlacedGlyph &other = spell.glyphs[i];
    const SvgAsset *otherAsset = nullptr;
    for (const auto &a : assets) {
      if (a.id == other.assetId) {
        otherAsset = &a;
        break;
      }
    }
    if (!otherAsset)
      continue;

    auto otherWorld = GlyphSegments(*otherAsset, other);

    if (SegmentsCrossAny(world, otherWorld))
      return false;
  }

  return true;
}

float PointToSegmentDistance(Vector2 p, const LineSeg &seg) {
  Vector2 ab{seg.b.x - seg.a.x, seg.b.y - seg.a.y};
  Vector2 ap{p.x - seg.a.x, p.y - seg.a.y};
  float abLen2 = ab.x * ab.x + ab.y * ab.y;
  if (abLen2 <= EPS)
    return std::hypot(ap.x, ap.y);

  float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLen2, 0.0f, 1.0f);
  Vector2 closest{seg.a.x + ab.x * t, seg.a.y + ab.y * t};
  return std::hypot(p.x - closest.x, p.y - closest.y);
}

bool HitTestGlyph(const PlacedGlyph &glyph, const SvgAsset &asset,
                  Vector2 canvasPoint, float threshold) {
  auto world = GlyphSegments(asset, glyph);

  for (const auto &seg : world) {
    if (PointToSegmentDistance(canvasPoint, seg) <= threshold)
      return true;
  }

  float centerDist =
      std::hypot(canvasPoint.x - glyph.position.x, canvasPoint.y - glyph.position.y);
  return centerDist <= threshold;
}

} // namespace SpellGeometry
