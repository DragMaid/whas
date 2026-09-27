#include "whas/ui/spell_thumbnails.h"
#include "whas/spell/spell_geometry.h"
#include "whas/spell/spell_system.h"
#include <cstring>

namespace {

uint64_t LayoutHash(const Spell &spell) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](const void *data, size_t n) {
    const auto *b = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 0x100000001b3ull;
    }
  };
  for (const PlacedGlyph &g : spell.glyphs) {
    mix(g.assetId.data(), g.assetId.size());
    float v[4] = {g.position.x, g.position.y, g.scale, g.rotationDeg};
    mix(v, sizeof v);
  }
  return h;
}

} // namespace

SpellThumbnails::~SpellThumbnails() {
  for (auto &[key, tex] : m_cache)
    UnloadRenderTexture(tex);
}

Color SpellThumbnails::Tint(const Spell &spell) {
  for (const PlacedGlyph &g : spell.glyphs) {
    if (g.kind != GlyphKind::Sigil)
      continue;
    if (g.assetId == "wind")
      return {170, 235, 180, 255};
    if (g.assetId == "gust")
      return {205, 225, 255, 255};
    switch (SpellSystem::SigilElement(g.assetId)) {
    case Element::WATER:
      return {90, 170, 255, 255};
    case Element::FIRE:
      return {255, 140, 60, 255};
    case Element::EARTH:
      return {200, 140, 90, 255};
    case Element::ICE:
      return {160, 235, 255, 255};
    case Element::SAND:
      return {235, 205, 120, 255};
    case Element::ROCK:
      return {185, 185, 195, 255};
    default:
      break;
    }
  }
  return {200, 200, 210, 255};
}

void SpellThumbnails::Render(const Spell &spell, RenderTexture2D &target) {
  const float half = SIZE * 0.5f;
  const float scale = (half - 3.0f) / SPELL_OUTER_RADIUS;
  auto toTex = [&](Vector2 p) {
    return Vector2{half + p.x * scale, half + p.y * scale};
  };
  Color tint = Tint(spell);
  Color faint{tint.r, tint.g, tint.b, 110};

  BeginTextureMode(target);
  ClearBackground(BLANK);
  DrawRing({half, half}, SPELL_OUTER_RADIUS * scale - 2.0f,
           SPELL_OUTER_RADIUS * scale, 0, 360, 64, tint);
  DrawCircleLinesV({half, half}, SPELL_INNER_RADIUS * scale, faint);
  for (const PlacedGlyph &glyph : spell.glyphs) {
    const SvgAsset *asset = m_glyphs.FindById(glyph.assetId);
    if (!asset)
      continue;
    auto segments = SpellGeometry::TransformSegments(
        asset->segments, asset->localCenter, glyph.position, glyph.scale,
        glyph.rotationDeg);
    Color c = glyph.kind == GlyphKind::Sigil ? tint : RAYWHITE;
    for (const LineSeg &seg : segments)
      DrawLineEx(toTex(seg.a), toTex(seg.b), 3.0f, c);
  }
  EndTextureMode();
  SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
}

const RenderTexture2D &SpellThumbnails::Get(const Spell &spell) {
  uint64_t key = LayoutHash(spell);
  auto it = m_cache.find(key);
  if (it != m_cache.end())
    return it->second;
  RenderTexture2D target = LoadRenderTexture(SIZE, SIZE);
  Render(spell, target);
  return m_cache.emplace(key, target).first->second;
}

void SpellThumbnails::Draw(ImDrawList *dl, const Spell &spell, ImVec2 min,
                           ImVec2 max, unsigned char alpha) {
  const RenderTexture2D &tex = Get(spell);
  // Render textures are stored upside down
  dl->AddImage((ImTextureID)(uintptr_t)tex.texture.id, min, max, {0, 1},
               {1, 0}, IM_COL32(255, 255, 255, alpha));
}
