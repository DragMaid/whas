#include "whas/game/character_draw.h"
#include "whas/constants.h"
#include <cstdint>

namespace {

Rectangle ToScreen(Rectangle cells) {
  return {cells.x * CELL_SIZE, cells.y * CELL_SIZE, cells.width * CELL_SIZE,
          cells.height * CELL_SIZE};
}

} // namespace

void DrawCharacterBody(const Character &c, Color color, bool drawHp) {
  Rectangle r = ToScreen(c.Bounds());
  DrawRectangleRec(r, color);
  DrawRectangleLinesEx(r, 1.0f, Color{20, 20, 30, color.a});

  // Eye to show facing
  float eyeX = r.x + r.width * (c.facing > 0 ? 0.7f : 0.3f);
  DrawCircleV({eyeX, r.y + r.height * 0.25f}, 1.5f, Color{20, 20, 30, color.a});

  // Flames wrapping the body, more of them the more stacks there are
  if (c.Burning()) {
    int frame = static_cast<int>(GetTime() * 14.0);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 3 + c.burnStacks * 2; ++i) {
      uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(frame * 40503u);
      h ^= h >> 15;
      float fx = r.x + (h % 100) / 100.0f * r.width;
      float fy = r.y + r.height * (0.2f + ((h >> 8) % 80) / 100.0f);
      float size = 2.0f + ((h >> 16) % 4);
      DrawTriangle({fx, fy - size * 2.2f}, {fx - size, fy}, {fx + size, fy},
                   Color{255, (unsigned char)(90 + (h >> 20) % 120), 20,
                         (unsigned char)(150 * color.a / 255)});
    }
    EndBlendMode();
  }

  if (!drawHp)
    return;
  float w = 28.0f;
  Vector2 top{r.x + r.width * 0.5f - w * 0.5f, r.y - 8.0f};
  DrawRectangleV(top, {w, 4.0f}, Color{40, 20, 20, 220});
  DrawRectangleV(top, {w * (c.hp / c.maxHp), 4.0f}, Color{90, 220, 110, 255});
}

