#pragma once

#include "whas/spell/spell_store.h"
#include "whas/spell/spell_types.h"
#include "whas/spell/svg_library.h"
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

class SpellEditor {
public:
  SpellEditor();

  void Draw();
  bool IsOpen() const { return m_open; }
  void Open() { m_open = true; }
  void Close() { m_open = false; }

private:
  void DrawOverlay();
  void DrawCanvas(ImVec2 canvasOrigin, ImVec2 canvasSize);
  void DrawPalette();
  void DrawEditPanel();
  void DrawSavedSpells();

  void DrawAssetThumbnail(const SvgAsset &asset, bool selected);
  void DrawGlyphLines(ImDrawList *dl, const SvgAsset &asset,
                      const PlacedGlyph &glyph, ImVec2 canvasOrigin,
                      ImVec2 canvasCenter, ImU32 color, float thickness);

  Vector2 CanvasToSpellSpace(ImVec2 canvasOrigin, ImVec2 canvasCenter,
                             ImVec2 screenPos) const;
  ImVec2 SpellToCanvasSpace(ImVec2 canvasOrigin, ImVec2 canvasCenter,
                            Vector2 spellPos) const;

  bool TryPlaceAt(Vector2 spellPos);
  bool TrySelectAt(Vector2 spellPos);
  void RefreshSavedSpells();

  const SvgAsset *GetAssetForGlyph(const PlacedGlyph &glyph) const;
  std::vector<LineSeg> GetWorldSegments(const PlacedGlyph &glyph,
                                        const SvgAsset &asset,
                                        ImVec2 canvasOrigin,
                                        ImVec2 canvasCenter) const;

  bool IsPlacementValid(const PlacedGlyph &candidate,
                        std::optional<size_t> ignoreIndex) const;

  void DrawClampedFloat(const char *label, float *value, float minV,
                        float maxV, float step);

  bool m_open = false;
  Spell m_currentSpell;
  std::vector<Spell> m_savedSpells;

  SvgLibrary m_library;
  SpellStore m_store;

  bool m_isPlacing = false;
  std::string m_paletteAssetId;
  int m_selectedGlyphIndex = -1;
  float m_ghostScale = 1.0f;
  float m_ghostRotation = 0.0f;

  char m_nameBuffer[SPELL_NAME_MAX_LEN + 1]{};
  std::string m_statusMessage;
};
