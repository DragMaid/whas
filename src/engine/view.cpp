#include "whas/engine/view.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

namespace View {

namespace {
Camera2D g_camera{{0, 0}, {0, 0}, 0.0f, 1.0f};

struct Following {
  bool on = false;
  bool snap = false;
  Vector2 want{0, 0}; // world pixels
  Vector2 at{0, 0};   // where the view is, easing toward want
  float zoom = 1.0f;
};
Following g_follow;
} // namespace

void Update() {
  float sw = static_cast<float>(GetScreenWidth());
  float sh = static_cast<float>(GetScreenHeight());
  constexpr float worldW = GRID_W * CELL_SIZE;
  constexpr float worldH = GRID_H * CELL_SIZE;
  float scale = std::max(0.1f, std::min(sw / worldW, sh / worldH));
  if (!g_follow.on) {
    g_camera.zoom = scale;
    g_camera.target = {0, 0};
    g_camera.offset = {(sw - worldW * scale) * 0.5f,
                       (sh - worldH * scale) * 0.5f};
    return;
  }

  float zoom = scale * g_follow.zoom;
  // Half the view in world pixels: keep it inside the world (centred when
  // the world is the smaller)
  float hw = sw * 0.5f / zoom, hh = sh * 0.5f / zoom;
  auto clampTo = [](float v, float half, float size) {
    return half * 2.0f >= size ? size * 0.5f : std::clamp(v, half, size - half);
  };
  Vector2 want{clampTo(g_follow.want.x, hw, worldW),
               clampTo(g_follow.want.y, hh, worldH)};
  if (g_follow.snap) {
    g_follow.at = want;
    g_follow.snap = false;
  } else {
    float k = 1.0f - std::exp(-8.0f * GetFrameTime());
    g_follow.at.x += (want.x - g_follow.at.x) * k;
    g_follow.at.y += (want.y - g_follow.at.y) * k;
  }
  g_camera.zoom = zoom;
  g_camera.target = g_follow.at;
  g_camera.offset = {sw * 0.5f, sh * 0.5f};
}

void Follow(Vector2 world, float zoom, bool snap) {
  g_follow.snap = snap || !g_follow.on;
  g_follow.on = true;
  g_follow.want = world;
  g_follow.zoom = zoom;
}

void StopFollowing() { g_follow.on = false; }

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
