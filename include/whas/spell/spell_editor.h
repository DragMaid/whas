#pragma once

#include "whas/spell/deck.h"
#include "whas/spell/spell_library.h"
#include "whas/spell/spell_system.h"
#include "whas/spell/spell_types.h"
#include "whas/spell/svg_library.h"
#include <imgui.h>
#include <optional>
#include <string>
#include <vector>

class SpellThumbnails;

class SpellEditor {
public:
  SpellEditor();

  // The editor edits the shared spell library and decks
  void Bind(SpellLibrary *spells, DeckBook *decks);
  void SetThumbnails(SpellThumbnails *thumbnails) { m_thumbnails = thumbnails; }
  const SvgLibrary &Glyphs() const { return m_library; }

  // "Test in sandbox" was clicked for this spell ref
  bool TakeTestRequest(std::string &ref);

  void Draw();
  bool IsOpen() const { return m_open; }
  void Open() { m_open = true; }
  // Straight to the spell grid and decks
  void OpenLibrary() {
    m_open = true;
    m_tab = Tab::Library;
    m_switchTab = true;
  }
  void Close() { m_open = false; }

  // Shared with the casting UI
  static Color BalanceColor(float imbalance);
  static void DrawStats(const SpellStats &stats);

private:
  void DrawOverlay();
  void DrawCanvas(ImVec2 canvasOrigin, ImVec2 canvasSize);
  void DrawPalette();
  void DrawEditPanel();
  // Library & Decks tab (spell_editor_library.cpp)
  void DrawLibraryTab();
  void DrawSpellGrid(float width);
  void DrawSpellCard(const Spell &spell, ImVec2 size);
  void DrawDeckPanel();
  void DrawDeckSlots(const Deck &deck);
  void DrawRoundDecks();
  void DrawLibraryPopups();
  void DrawPreviewStrip(ImVec2 size);
  void OpenSpell(const Spell &spell);
  void DrawVectorOverlay(ImDrawList *dl, ImVec2 canvasOrigin,
                         ImVec2 canvasCenter);

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
  void SaveCurrent();

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

  SvgLibrary m_library; // glyph shapes
  SpellLibrary *m_spells = nullptr;
  DeckBook *m_decks = nullptr;
  SpellThumbnails *m_thumbnails = nullptr;

  // Library & Decks tab
  enum class Tab { Edit, Library };
  Tab m_tab = Tab::Edit;
  bool m_switchTab = false;
  char m_search[48]{};
  int m_filter = 0; // index into the sigil filter chips, 0 = all
  std::string m_selectedDeck;
  std::string m_popupRef;       // spell being renamed or deleted
  std::string m_popupDeck;      // deck being renamed
  char m_renameBuffer[SPELL_NAME_MAX_LEN + 1]{};
  const char *m_openPopup = nullptr;
  std::string m_testRef;

  bool m_isPlacing = false;
  std::string m_paletteAssetId;
  int m_selectedGlyphIndex = -1;
  float m_ghostScale = 1.0f;
  float m_ghostRotation = 0.0f;

  char m_nameBuffer[SPELL_NAME_MAX_LEN + 1]{};
  std::string m_statusMessage;
};
