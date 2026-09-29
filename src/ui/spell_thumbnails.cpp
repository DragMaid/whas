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
  auto glyphs = [&](const std::vector<PlacedGlyph> &list) {
    for (const PlacedGlyph &g : list) {
      mix(g.assetId.data(), g.assetId.size());
      float v[5] = {g.position.x, g.position.y, g.scale, g.rotationDeg,
                    g.inverted ? 1.0f : 0.0f};
      mix(v, sizeof v);
    }
  };
  glyphs(spell.glyphs);
  for (const SpellComponent &c : spell.components) {
    float v[4] = {c.position.x, c.position.y, c.scale, c.rotationDeg};
    mix(v, sizeof v);
    glyphs(c.glyphs);
  }
  return h;
}

} // namespace

SpellThumbnails::~SpellThumbnails() {
  for (auto &[key, tex] : m_cache)
    UnloadRenderTexture(tex);
}

Color SpellThumbnails::Tint(const Spell &spell) {
  // A layered spell takes the colour of its first part
  if (spell.Layered()) {
    Spell first;
    first.glyphs = spell.components.front().glyphs;
    return Tint(first);
  }
  for (const PlacedGlyph &g : spell.glyphs) {
    if (g.kind != GlyphKind::Sigil)
      continue;
    if (g.assetId == "wind_underfoot")
      return {170, 235, 180, 255};
    if (g.assetId == "wind")
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
  auto drawGlyph = [&](const PlacedGlyph &glyph, float thickness) {
    const SvgAsset *asset = m_glyphs.FindById(glyph.assetId);
    if (!asset)
      return;
    Color c = glyph.kind == GlyphKind::Sigil ? tint : RAYWHITE;
    for (const LineSeg &seg : SpellGeometry::GlyphSegments(*asset, glyph))
      DrawLineEx(toTex(seg.a), toTex(seg.b), thickness, c);
  };
  if (spell.Layered()) {
    DrawCircleLinesV({half, half}, LAYER_RING_INNER * scale, faint);
    for (const SpellComponent &comp : spell.components) {
      Vector2 c = toTex(comp.position);
      DrawCircleLinesV(c, SpellGeometry::ComponentRadius(comp.scale) * scale,
                       tint);
      for (const PlacedGlyph &glyph : comp.glyphs)
        drawGlyph(SpellGeometry::ComponentGlyph(comp, glyph), 2.0f);
    }
  } else {
    DrawCircleLinesV({half, half}, SPELL_INNER_RADIUS * scale, faint);
  }
  for (const PlacedGlyph &glyph : spell.glyphs)
    drawGlyph(glyph, 3.0f);
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
