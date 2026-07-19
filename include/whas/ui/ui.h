#pragma once
#include "whas/core/element.h"
#include "whas/engine/simulation.h"
#include "whas/spell/spell_editor.h"
#include "whas/spell/spell_store.h"
#include "whas/spell/spell_system.h"
#include <raylib.h>
#include <vector>

struct UIState {
  Element selectedMaterial = Element::WATER;
  int brushRadius = 2;
  bool debugOverlay = true;
  bool showConfigEditor = true;
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

private:
  void DrawPropertyEditor(SimulationConfig &config);
  void DrawElementPropertyEditor(SimulationConfig &config);
  void DrawInspector(Simulation &sim);
  void DrawSpellButton();
  void DrawSpellSelectionPanel();
  void DrawSpellAimPreview(const Simulation &sim);
  void DrawActiveSpellEffects(const Simulation &sim);
  Color GetSpellColor(const Spell &spell) const;
  const char* GetElementName(Element element) const;

  static constexpr int PANEL_HEIGHT = 60;
  static constexpr int PANEL_Y = 660;
  static constexpr int BTN_W = 80;
  static constexpr int BTN_H = 40;
  static constexpr int BTN_PAD = 6;
  static constexpr int SPELL_BTN_W = 100;
  static constexpr int SPELL_BTN_H = 32;

  struct Button {
    Rectangle rect;
    Element element;
    const char *label;
    Color col;
  };

  Button m_buttons[9];
  Rectangle m_spellButton{};
  SpellEditor m_spellEditor;

  // Spell casting state
  SpellStore m_spellStore;
  std::vector<Spell> m_availableSpells;
  int m_selectedSpellIndex = -1;
  bool m_isAimingSpell = false;
  Vector2 m_spellOrigin{0, 0};
  Vector2 m_spellAimDir{1, 0};
};
