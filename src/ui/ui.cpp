#include "whas/ui/ui.h"
#include "raylib.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

UI::UI() {
  m_buttons[0] = {{8 + 0 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::WATER,
                  "WATER",
                  {64, 164, 223, 255}};

  m_buttons[1] = {{8 + 1 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::EARTH,
                  "EARTH",
                  {100, 60, 20, 255}};
  m_buttons[2] = {{8 + 2 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::FIRE,
                  "FIRE",
                  {220, 80, 0, 255}};
  m_buttons[3] = {{8 + 3 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::STEAM,
                  "STEAM",
                  {180, 180, 200, 255}};
  m_buttons[4] = {{8 + 4 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::CLOUD,
                  "CLOUD",
                  {220, 220, 255, 255}};
  m_buttons[5] = {{8 + 5 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::ICE,
                  "ICE",
                  {150, 240, 255, 255}};
  m_buttons[6] = {{8 + 6 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::AIR,
                  "Eraser",
                  {60, 60, 60, 255}};
}

void UI::HandleInput(UIState &state) {
  // Keyboard shortcuts for materials
  if (IsKeyPressed(KEY_ONE))
    state.selectedMaterial = Element::WATER;
  if (IsKeyPressed(KEY_TWO))
    state.selectedMaterial = Element::EARTH;
  if (IsKeyPressed(KEY_THREE))
    state.selectedMaterial = Element::FIRE;
  if (IsKeyPressed(KEY_FOUR))
    state.selectedMaterial = Element::STEAM;
  if (IsKeyPressed(KEY_FIVE))
    state.selectedMaterial = Element::CLOUD;
  if (IsKeyPressed(KEY_SIX))
    state.selectedMaterial = Element::ICE;
  if (IsKeyPressed(KEY_SEVEN))
    state.selectedMaterial = Element::AIR;

  if (IsKeyPressed(KEY_F3))
    state.debugOverlay = !state.debugOverlay;

  // Brush size
  float wheel = GetMouseWheelMove();
  if (wheel != 0) {
    state.brushRadius = std::clamp(state.brushRadius + (int)wheel, 1, 20);
  }

  // Mouse clicks on buttons
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();
    for (const auto &btn : m_buttons) {
      if (CheckCollisionPointRec(m, btn.rect)) {
        state.selectedMaterial = btn.element;
      }
    }
  }
}

void UI::Draw(const UIState &state) {
  // Panel background
  DrawRectangle(0, PANEL_Y, WINDOW_WIDTH, PANEL_HEIGHT, Color{30, 30, 40, 255});
  DrawLine(0, PANEL_Y, WINDOW_WIDTH, PANEL_Y, DARKGRAY);

  // Buttons
  for (const auto &btn : m_buttons) {
    bool selected = (state.selectedMaterial == btn.element);
    DrawRectangleRec(btn.rect, selected ? WHITE : btn.col);
    DrawRectangleLinesEx(btn.rect, 2, selected ? BLACK : DARKGRAY);

    int textX = (int)btn.rect.x + (BTN_W - MeasureText(btn.label, 14)) / 2;
    int textY = (int)btn.rect.y + (BTN_H - 14) / 2;
    DrawText(btn.label, textX, textY, 14, selected ? BLACK : WHITE);
  }

  // Current selection info
  DrawText(TextFormat("Brush: %d", state.brushRadius), WINDOW_WIDTH - 120,
           PANEL_Y + 20, 16, RAYWHITE);
}

Vector2 UI::GetMouseCell() const {
  Vector2 m = GetMousePosition();
  return {std::floor(m.x / CELL_SIZE), std::floor(m.y / CELL_SIZE)};
}

bool UI::IsMouseOverPanel() const { return GetMouseY() >= PANEL_Y; }
