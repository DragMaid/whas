#pragma once
#include "whas/core/element.h"
#include "whas/engine/simulation.h"
#include "whas/spell/deck.h"
#include "whas/spell/spell_editor.h"
#include "whas/spell/spell_library.h"
#include "whas/spell/spell_system.h"
#include "whas/ui/spell_thumbnails.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <raylib.h>
#include <string>
#include <vector>

// What left-click does in the sandbox; never both at once
enum class SandboxTool { Draw, Cast };

// How the time-stop button looks and what clicking it means
enum class ClockLook {
  Running,   // time flows; click to stop it
  Stopped,   // time stopped (planning); click to let it flow
  Executing, // a turn is playing out; not clickable
  Waiting,   // between turns; click to stop time and plan
  Over,      // round or match over
};

struct UIState {
  Element selectedMaterial = Element::WATER;
  int brushRadius = 2;
  bool debugOverlay = false;
  bool showConfigEditor = false;
  SandboxTool tool = SandboxTool::Cast;

  // Filled in each frame by whoever owns the clock (Game or Sandbox)
  ClockLook clock = ClockLook::Running;
  float clockProgress = 0.0f; // 0..1 of the turn used or played out
  int ticksFree = 180;        // channel time left, for greying out slots
  int matchRound = -1;        // >= 0 in a match: that round's deck is locked

  // Online matches play by the default rules: tuning sliders are hidden
  bool configLocked = false;

  // Requests from the bar, handled by main
  bool menuRequested = false;
  bool timeToggleRequested = false;
  bool resetAvatarRequested = false;
  bool sandboxRequested = false; // "Test in sandbox" from the editor
};

class UI {
public:
  UI();
  ~UI();

  void HandleInput(UIState &state, Simulation &sim);
  void Draw(UIState &state, Simulation &sim);
  Vector2 GetMouseCell() const;
  bool IsMouseOverPanel() const;
  bool IsBlockingWorldInput() const;

  void OpenSpellLibrary() { m_spellEditor.OpenLibrary(); }

  // Game mode hands the mouse to the character instead of the sandbox tools
  void SetGameMode(bool enabled);
  // Spell in the selected hotbar slot (null for an empty slot)
  const Spell *GetSelectedSpell() const;
  int GetSelectedSlot() const { return m_selectedSlot; }

  // Online: the hotbar shows the round's cards as the server locked them
  void SetMatchSpells(std::array<std::optional<Spell>, DECK_SLOTS> spells) {
    m_matchSpells = std::move(spells);
    m_hasMatchSpells = true;
  }
  void ClearMatchSpells() { m_hasMatchSpells = false; }

  // Extra ImGui windows (the play menu, replay controls) drawn each frame
  void SetOverlay(std::function<void()> draw) { m_overlay = std::move(draw); }

  SpellLibrary &Library() { return m_library; }
  DeckBook &Decks() { return m_decks; }
  // Beam preview plus aim-vs-cast arrows, origin in (fractional) cells.
  // worldGravity is the config's world gravity, for the falling arcs.
  void DrawAimIndicator(const Spell &spell, Vector2 originCells, Vector2 aimDir,
                        float worldGravity) const;
  // Vector outline of where a cast goes: an element beam with its fall-off
  // arc, a gust's wind field, or the caster's flight arc
  void DrawSpellBeam(const SpellStats &stats, Vector2 originCells,
                     Vector2 castDir, Color color, float worldGravity) const;
  Color GetSpellColor(const Spell &spell) const;

  static constexpr int BAR_HEIGHT = 60;
  static constexpr int BAR_Y = WINDOW_HEIGHT - BAR_HEIGHT;

private:
  void DrawPropertyEditor(SimulationConfig &config);
  void DrawElementPropertyEditor(SimulationConfig &config);
  void DrawInspector(Simulation &sim);
  void DrawActiveGusts(const Simulation &sim) const;

  // Bottom bar: time-stop button, Draw|Cast toggle, hotbar or materials,
  // deck picker (action_bar.cpp)
  void DrawActionBar(UIState &state);
  void DrawTimeButton(UIState &state, ImVec2 pos, float size);
  void DrawHotbar(UIState &state, ImVec2 pos, ImVec2 size);
  void DrawMaterials(UIState &state, ImVec2 pos, ImVec2 size);
  void DrawDeckPicker(UIState &state, ImVec2 pos, ImVec2 size);

  // The deck shown in the hotbar: the round's deck in a match, else the
  // active one
  const Deck *HotbarDeck(const UIState &state) const;
  const Spell *SlotSpell(const UIState &state, int slot) const;
  void SelectSlot(int slot);

  // Editor requests (rename/delete/test) that touch decks and the hotbar
  void HandleEditorRequests(UIState &state);

  SpellLibrary m_library;
  DeckBook m_decks;
  SpellEditor m_spellEditor;
  std::unique_ptr<SpellThumbnails> m_thumbnails;

  int m_selectedSlot = 0;
  std::array<std::optional<Spell>, DECK_SLOTS> m_matchSpells{};
  bool m_hasMatchSpells = false;
  std::function<void()> m_overlay;
  // "Test in sandbox" puts a spell in slot 1 without touching saved decks
  std::string m_testSpellRef;
  const UIState *m_lastState = nullptr;
  bool m_gameMode = false;
};
