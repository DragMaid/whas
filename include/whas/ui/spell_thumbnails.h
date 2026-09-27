#pragma once
#include "whas/spell/spell_types.h"
#include "whas/spell/svg_library.h"
#include <cstdint>
#include <imgui.h>
#include <raylib.h>
#include <unordered_map>

// Spell circles rendered once into textures, for hotbar slots and editor
// cards. Keyed by the glyph layout, so an edited spell gets a new image.
class SpellThumbnails {
public:
  static constexpr int SIZE = 128;

  explicit SpellThumbnails(const SvgLibrary &glyphs) : m_glyphs(glyphs) {}
  ~SpellThumbnails();
  SpellThumbnails(const SpellThumbnails &) = delete;
  SpellThumbnails &operator=(const SpellThumbnails &) = delete;

  // Into an ImGui draw list (cards, hotbar)
  void Draw(ImDrawList *dl, const Spell &spell, ImVec2 min, ImVec2 max,
            unsigned char alpha = 255);

  // Colour of a spell's sigil element, used to tint its circle
  static Color Tint(const Spell &spell);

private:
  const RenderTexture2D &Get(const Spell &spell);
  void Render(const Spell &spell, RenderTexture2D &target);

  const SvgLibrary &m_glyphs;
  std::unordered_map<uint64_t, RenderTexture2D> m_cache;
};
