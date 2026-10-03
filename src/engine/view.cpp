#include "whas/engine/view.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

namespace View {

namespace {
Camera2D g_camera{{0, 0}, {0, 0}, 0.0f, 1.0f};
} // namespace

void Update() {
  float sw = static_cast<float>(GetScreenWidth());
  float sh = static_cast<float>(GetScreenHeight());
  constexpr float worldW = GRID_W * CELL_SIZE;
  constexpr float worldH = GRID_H * CELL_SIZE;
  float scale = std::max(0.1f, std::min(sw / worldW, sh / worldH));
  g_camera.zoom = scale;
  g_camera.offset = {(sw - worldW * scale) * 0.5f,
                     (sh - worldH * scale) * 0.5f};
}

Camera2D Camera() { return g_camera; }
float Scale() { return g_camera.zoom; }

Vector2 MouseWorld() { return GetScreenToWorld2D(GetMousePosition(), g_camera); }

Vector2 MouseCells() {
  Vector2 w = MouseWorld();
  return {w.x / CELL_SIZE, w.y / CELL_SIZE};
}

Vector2 WorldToScreen(Vector2 world) {
  return GetWorldToScreen2D(world, g_camera);
}

float UiScale() {
  float s = std::clamp(GetScreenHeight() / 720.0f, 1.0f, 2.5f);
  return std::floor(s * 4.0f) / 4.0f;
}

} // namespace View
