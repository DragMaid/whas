#include "whas/engine/view.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

namespace View {

namespace {
Camera2D g_camera{{0, 0}, {0, 0}, 0.0f, 1.0f};
Insets g_insets;
bool g_following = false;
Vector2 g_target{0, 0};
float g_follow = 1.0f;
} // namespace

void SetInsets(Insets insets) { g_insets = insets; }

void Update() {
  float sw = std::max(1.0f, GetScreenWidth() - g_insets.left - g_insets.right);
  float sh = std::max(1.0f, GetScreenHeight() - g_insets.top - g_insets.bottom);
  constexpr float worldW = GRID_W * CELL_SIZE;
  constexpr float worldH = GRID_H * CELL_SIZE;
  float scale = std::max(0.1f, std::min(sw / worldW, sh / worldH));
  if (!g_following || g_follow <= 1.0f) {
    g_camera.zoom = scale;
    g_camera.target = {0, 0};
    g_camera.offset = {g_insets.left + (sw - worldW * scale) * 0.5f,
                       g_insets.top + (sh - worldH * scale) * 0.5f};
    return;
  }
  // Zoomed in: fill the screen where the world is big enough, keep the
  // view inside it
  scale = std::max(scale * g_follow, std::max(sw / worldW, sh / worldH));
  float halfW = sw * 0.5f / scale, halfH = sh * 0.5f / scale;
  Vector2 t{std::clamp(g_target.x, std::min(halfW, worldW * 0.5f),
                       std::max(worldW - halfW, worldW * 0.5f)),
            std::clamp(g_target.y, std::min(halfH, worldH * 0.5f),
                       std::max(worldH - halfH, worldH * 0.5f))};
  g_camera.zoom = scale;
  g_camera.target = t;
  g_camera.offset = {g_insets.left + sw * 0.5f, g_insets.top + sh * 0.5f};
}

void Follow(Vector2 target, float zoom) {
  g_following = true;
  g_target = target;
  g_follow = zoom;
  Update();
}

void Unfollow() {
  if (!g_following)
    return;
  g_following = false;
  Update();
}

Vector2 Target() {
  if (g_following)
    return g_camera.target;
  return {GRID_W * CELL_SIZE * 0.5f, GRID_H * CELL_SIZE * 0.5f};
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
