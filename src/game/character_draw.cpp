#include "whas/game/character_draw.h"
#include "whas/constants.h"
#include <cstdint>

namespace {

// Sprite sheets are rows of square frames, drawn as the art faces (right)
constexpr int FRAME_SIZE = 32;
// Whole frames per sprite pixel keep the art crisp; one frame is 8 cells
// tall, taller than the 6-cell body, so the hat rises above it
constexpr float SPRITE_SCALE = 2.0f;
constexpr float IDLE_FPS = 6.0f;
// Below this the body is taken to be at the top of its arc, not rising
constexpr float RISE_SPEED = 1.0f; // cells/s

struct Sprites {
  Texture2D idle{};
  Texture2D fly{};
  Texture2D fall{};
  bool loaded = false;
};

Sprites &GetSprites() {
  static Sprites s;
  if (!s.loaded) {
    s.idle = LoadTexture("assets/animations/idle.png");
    s.fly = LoadTexture("assets/animations/fly.png");
    s.fall = LoadTexture("assets/animations/fall.png");
    s.loaded = true;
  }
  return s;
}

Rectangle ToScreen(Rectangle cells) {
  return {cells.x * CELL_SIZE, cells.y * CELL_SIZE, cells.width * CELL_SIZE,
          cells.height * CELL_SIZE};
}

} // namespace

void DrawCharacterBody(const Character &c, Color color, bool drawHp) {
  Rectangle r = ToScreen(c.Bounds());

  // Idle on the ground, fly while rising, fall while dropping
  const Sprites &sprites = GetSprites();
  const Texture2D *sheet = &sprites.idle;
  if (!c.grounded)
    sheet = c.vel.y < -RISE_SPEED ? &sprites.fly : &sprites.fall;
  int frames = sheet->width / FRAME_SIZE;
  int frame = frames > 1 ? (int)(GetTime() * IDLE_FPS) % frames : 0;

  // Mirror when facing left
  Rectangle src{(float)(frame * FRAME_SIZE), 0.0f, (float)FRAME_SIZE,
                (float)FRAME_SIZE};
  if (c.look < 0)
    src.width = -src.width;

  // Feet on the body's bottom edge, centred on it
  float spriteSize = FRAME_SIZE * SPRITE_SCALE;
  Rectangle dst{r.x + r.width * 0.5f - spriteSize * 0.5f,
                r.y + r.height - spriteSize, spriteSize, spriteSize};
  // Sprites keep their own colours; the slot colour shows on the marker
  Color tint = c.Alive() ? WHITE : GRAY;
  tint.a = color.a;
  DrawTexturePro(*sheet, src, dst, {0.0f, 0.0f}, 0.0f, tint);

  // Flames wrapping the body, more of them the more stacks there are
  if (c.Burning()) {
    int flameFrame = static_cast<int>(GetTime() * 14.0);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 3 + c.burnStacks * 2; ++i) {
      uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(flameFrame * 40503u);
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
  Vector2 top{r.x + r.width * 0.5f - w * 0.5f, dst.y - 6.0f};
  DrawRectangleV(top, {w, 4.0f}, Color{40, 20, 20, 220});
  DrawRectangleV(top, {w * (c.hp / c.maxHp), 4.0f}, Color{90, 220, 110, 255});

  // Whose body this is, now that the body itself isn't painted in the colour
  float mx = r.x + r.width * 0.5f;
  DrawTriangle({mx - 3.0f, top.y - 6.0f}, {mx, top.y - 2.0f},
               {mx + 3.0f, top.y - 6.0f}, color);
}

void UnloadCharacterSprites() {
  Sprites &s = GetSprites();
  UnloadTexture(s.idle);
  UnloadTexture(s.fly);
  UnloadTexture(s.fall);
  s = Sprites{};
}

