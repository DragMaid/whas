#pragma once
#include "whas/core/element.h"
#include "whas/engine/simulation.h"
#include <raylib.h>

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

  void HandleInput(UIState &state);
  void Draw(UIState &state, Simulation &sim);
  Vector2 GetMouseCell() const;
  bool IsMouseOverPanel() const;

private:
  void DrawPropertyEditor(SimulationConfig &config);
  void DrawElementPropertyEditor(SimulationConfig &config);
  void DrawInspector(Simulation &sim);
  const char* GetElementName(Element element) const;

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

  Button m_buttons[9];
};
