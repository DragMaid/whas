#pragma once
#include "whas/core/element.h"
#include <imgui.h>
#include <raylib.h>
#include <vector>

class Simulation;

// The terrain brush both editors paint with: a palette of element swatches
// in their world colours, keys 1-9 for the elements and E for the eraser,
// [ and ] (or the wheel) for the size, and Ctrl+Z to take back a stroke.
class TerrainBrush {
public:
  static constexpr int MAX_SIZE = 20;

  Element element = Element::EARTH;
  int size = 3;

  // Shortcuts; call while the editor has the keyboard
  void HandleKeys(Simulation &sim);
  // Left paints, right erases, under the mouse (cells), unless over a panel
  void Paint(Simulation &sim, Vector2 cell, bool overUi);
  void Undo(Simulation &sim);
  bool CanUndo() const { return !m_undo.empty(); }
  void ClearHistory() { m_undo.clear(); }

  // A row of swatches and the size, for a toolbar (ImGui)
  void DrawPalette();
  // The brush ring in the element's colour (inside the world camera)
  void DrawCursor(Vector2 cell) const;

  // How an element looks in the world, for swatches
  static Color Swatch(Element e);

private:
  std::vector<std::vector<uint8_t>> m_undo; // the world before each stroke
  bool m_stroke = false;
};
