#pragma once

#include <raylib.h>
#include <string>
#include <vector>

constexpr float SPELL_OUTER_RADIUS = 250.0f;
constexpr float SPELL_INNER_RADIUS = 200.0f;
constexpr float GLYPH_SCALE_MIN = 0.1f;
constexpr float GLYPH_SCALE_MAX = 3.0f;
constexpr float GLYPH_ROTATION_MIN = -180.0f;
constexpr float GLYPH_ROTATION_MAX = 180.0f;
constexpr int SPELL_NAME_MAX_LEN = 32;

// Layered spells: the embedded spells sit in the core, the outer ring band
// holds signs that modify every one of them. Only one level of nesting.
// The core takes 70% of the circle, so a lone embedded spell reads clearly
constexpr float LAYER_CORE_RADIUS = 175.0f;
constexpr float LAYER_RING_INNER = 183.0f;
constexpr float LAYER_RING_OUTER = SPELL_OUTER_RADIUS - 4.0f;
constexpr int LAYER_MAX_COMPONENTS = 5;
// Component scale is its radius as a share of a full circle's
constexpr float COMPONENT_SCALE_MIN = 0.2f;
constexpr float COMPONENT_SCALE_MAX = LAYER_CORE_RADIUS / SPELL_OUTER_RADIUS;

// Drawn rings are left open by this many degrees at the bottom, like the
// circles in the manga
constexpr float RING_GAP_DEG = 14.0f;

enum class GlyphKind { Sign, Sigil };

struct LineSeg {
  Vector2 a;
  Vector2 b;
};

struct SvgAsset {
  std::string id;
  GlyphKind kind;
  std::string path;
  Vector2 localCenter{0, 0};
  float viewWidth = 64.0f;
  float viewHeight = 64.0f;
  std::vector<LineSeg> segments;
  // From "<id>.inverted.svg", or the shape turned 180 degrees
  std::vector<LineSeg> invertedSegments;

  const std::vector<LineSeg> &SegmentsFor(bool inverted) const {
    return inverted ? invertedSegments : segments;
  }
};

struct PlacedGlyph {
  std::string assetId;
  GlyphKind kind = GlyphKind::Sign;
  Vector2 position{0, 0};
  float scale = 1.0f;
  float rotationDeg = 0.0f;
  bool inverted = false; // only crushing and expansion can be inverted
};

// A single-layer spell embedded in a layered one. The glyphs are a copy, so
// renaming or deleting the source spell doesn't change the layered spell.
struct SpellComponent {
  std::string source; // name of the spell it was copied from, for display
  std::vector<PlacedGlyph> glyphs;
  Vector2 position{0, 0};
  float scale = 0.35f;      // COMPONENT_SCALE_MIN..MAX
  float rotationDeg = 0.0f; // turns the component's aim
};

struct Spell {
  std::string name;
  // In a layered spell these are the outer ring's signs
  std::vector<PlacedGlyph> glyphs;
  std::vector<SpellComponent> components;

  bool Layered() const { return !components.empty(); }
};
