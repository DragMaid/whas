#pragma once
#include "raylib.h"
#include "whas/core/element.h"

struct UIState {
  Element selectedMaterial = Element::WATER;
  // SpellType selectedSpell = SpellType::Fireball;
  int brushRadius = 2;
  bool debugOverlay = false;
};

class UI {
public:
  UI();

  // Handle keyboard / mouse-wheel input for element selection & brush.
  void HandleInput(UIState &state);

  // Draw the panel at the bottom of the screen.
  void Draw(const UIState &state);

  // Returns the grid-cell position of the mouse cursor.
  Vector2 GetMouseCell() const;

  // True if the mouse is hovering over the UI panel (don't paint).
  bool IsMouseOverPanel() const;

private:
  static constexpr int PANEL_HEIGHT = 60;
  static constexpr int PANEL_Y = 660;
  static constexpr int BTN_W = 80;
  static constexpr int BTN_H = 40;
  static constexpr int BTN_PAD = 6;

  struct Button {
    Rectangle rect;
    Element element;
    const char *label;
    Color col;
  };

  Button m_buttons[7];
};
