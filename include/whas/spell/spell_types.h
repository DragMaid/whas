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
};

struct PlacedGlyph {
  std::string assetId;
  GlyphKind kind = GlyphKind::Sign;
  Vector2 position{0, 0};
  float scale = 1.0f;
  float rotationDeg = 0.0f;
};

struct Spell {
  std::string name;
  std::vector<PlacedGlyph> glyphs;
};
