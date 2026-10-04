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
  // Real-time matches: how much of each slot's cooldown is left (0..1)
  std::array<float, DECK_SLOTS> cooldowns{};
  // Share of the wet paper's drying time left (0 = dry): only flight casts
  float wet = 0.0f;

  // Online matches play by the default rules: tuning sliders are hidden
  bool configLocked = false;
  // The map editor has the screen: no action bar
  bool hideActionBar = false;
  // A campaign (playing or editing) has the keys: no hotbar, editor or menu
  // shortcuts
  bool keysTaken = false;

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
  // World-space overlays (fields, debug bodies), inside the view's camera
  void DrawWorld(const UIState &state, Simulation &sim);
  // Screen-space UI on top
  void Draw(UIState &state, Simulation &sim);
  Vector2 GetMouseCell() const;
  bool IsMouseOverPanel() const;
  bool IsBlockingWorldInput() const;

  void OpenSpellLibrary() { m_spellEditor.OpenLibrary(); }
  void OpenSpellEditor() { m_spellEditor.Open(); }

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

  // Blinded by a light burst for `seconds` (the longest so far wins). While
  // `hold` stays true the screen stays white; after that it fades out.
  void Blind(float seconds, bool hold);

  // Extra ImGui windows (the play menu, replay controls) drawn each frame
  void SetOverlay(std::function<void()> draw) { m_overlay = std::move(draw); }

  SpellLibrary &Library() { return m_library; }
  SpellThumbnails &Thumbnails() { return *m_thumbnails; }
  const SvgLibrary &Glyphs() const { return m_spellEditor.Glyphs(); }
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

  // The bottom bar, in screen pixels (it grows with the UI scale)
  static float BarHeight();
  static float BarY();

private:
  void DrawPropertyEditor(SimulationConfig &config);
  void DrawElementPropertyEditor(SimulationConfig &config);
  void DrawInspector(Simulation &sim);
  void DrawActiveFields(const Simulation &sim) const;
  // Columns at work: their outline and how long they hold
  void DrawActiveColumns(const Simulation &sim) const;
  void DrawBlindness();
  // Restyle ImGui when the UI scale changes
  void ApplyUiScale();
  ImGuiStyle m_baseStyle;
  float m_uiScale = 0.0f;
  float m_blind = 0.0f; // seconds of blindness left
  bool m_blindHold = false;

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
