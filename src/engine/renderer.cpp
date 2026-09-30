#include "whas/engine/renderer.h"
#include "whas/constants.h"
#include "whas/core/element.h"
#include <algorithm>
#include <cstdio>

// Linearly interpolate between two colors.
static Color LerpColor(Color a, Color b, float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return Color{(unsigned char)(a.r + (b.r - a.r) * t),
               (unsigned char)(a.g + (b.g - a.g) * t),
               (unsigned char)(a.b + (b.b - a.b) * t),
               (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Render-only noise in [0, 1) that changes a dozen times a second, so flames
// flicker without touching the (deterministic) simulation
static float Flicker(int x, int y, int frame) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u +
               static_cast<uint32_t>(y) * 668265263u +
               static_cast<uint32_t>(frame) * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}

static Color Scale(Color c, float k) {
  return Color{(unsigned char)std::min(255.0f, c.r * k),
               (unsigned char)std::min(255.0f, c.g * k),
               (unsigned char)std::min(255.0f, c.b * k), c.a};
}

void Renderer::DrawWorld(const Simulation &sim) {
  if (m_worldTexture.id == 0) {
    m_pixels.assign(GRID_W * GRID_H, BLANK);
    Image image{m_pixels.data(), GRID_W, GRID_H, 1,
                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    m_worldTexture = LoadTextureFromImage(image);
    SetTextureFilter(m_worldTexture, TEXTURE_FILTER_POINT);
  }

  int flickerFrame = static_cast<int>(GetTime() * 12.0);
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      const Cell &c = sim.GetCell(x, y);
      Color col = CellColor(c);
      if (c.element == Element::FIRE || (c.flags & CELL_BURNING))
        col = Scale(col, 0.75f + 0.45f * Flicker(x, y, flickerFrame));
      m_pixels[y * GRID_W + x] = col;
    }
  }
  UpdateTexture(m_worldTexture, m_pixels.data());
  DrawTexturePro(m_worldTexture, {0, 0, (float)GRID_W, (float)GRID_H},
                 {0, 0, (float)(GRID_W * CELL_SIZE), (float)(GRID_H * CELL_SIZE)},
                 {0, 0}, 0.0f, WHITE);

  // Additive glow around flames: one soft disc per 2x2 block that has fire
  BeginBlendMode(BLEND_ADDITIVE);
  for (int y = 0; y < GRID_H; y += 2) {
    for (int x = 0; x < GRID_W; x += 2) {
      const Cell &c = sim.GetCell(x, y);
      if (c.element != Element::FIRE && !(c.flags & CELL_BURNING))
        continue;
      float f = Flicker(x, y, flickerFrame);
      unsigned char a = (unsigned char)(28 + 30 * f);
      DrawCircle(x * CELL_SIZE + CELL_SIZE, y * CELL_SIZE + CELL_SIZE,
                 CELL_SIZE * (2.2f + 1.2f * f), Color{255, 120, 30, a});
    }
  }
  EndBlendMode();

  // Draw particles (needs to cast away const for Draw() if it's not marked const, 
  // but it calls raylib DrawPixel which is global anyway)
  const_cast<Simulation&>(sim).GetParticleSystem().Draw();
}

void Renderer::DrawDebugOverlay(const Simulation &sim) {
  char buf[256];
  int lineH = 18;
  int px = 8, py = 8;

  snprintf(buf, sizeof(buf), "FPS: %d", GetFPS());
  DrawText(buf, px, py, 16, LIME);
  py += lineH;

  snprintf(buf, sizeof(buf), "Active Chunks: %d / %d", sim.GetActiveChunks(),
           CHUNK_COLS * CHUNK_ROWS);
  DrawText(buf, px, py, 16, LIME);
  py += lineH;

  snprintf(buf, sizeof(buf), "Particles: %d", sim.GetParticleCount());
  DrawText(buf, px, py, 16, LIME);
  py += lineH;

  snprintf(buf, sizeof(buf), "Avg Pressure: %.2f", sim.GetAvgPressure());
  DrawText(buf, px, py, 16, LIME);
  py += lineH;

  snprintf(buf, sizeof(buf), "Avg Temp: %.1f C", sim.GetAvgTemp());
  DrawText(buf, px, py, 16, LIME);
  py += lineH;

  DrawText("F3 = toggle debug", px, py, 14, DARKGRAY);
  py += lineH;
}

Color Renderer::CellColor(const Cell &c) const {
  Color base = BaseCellColor(c);
  if (c.flags & CELL_BURNING) {
    // Glowing embers eating through the material
    float fuelT = std::max(0.0f, std::min(c.lifetime / 2.0f, 1.0f));
    return LerpColor(Color{255, 90, 20, 255}, Color{255, 200, 80, 255},
                     fuelT * 0.6f);
  }
  if (c.flags & CELL_CHARRED)
    return LerpColor(base, Color{25, 20, 18, base.a}, 0.55f);
  return base;
}

Color Renderer::BaseCellColor(const Cell &c) const {
  // Helper to clamp values between 0.0f and 1.0f
  auto Clamp01 = [](float val) { return std::max(0.0f, std::min(val, 1.0f)); };

  switch (c.element) {
  case Element::AIR: {
    // High pressure air looks compressed and heavy
    float pT = Clamp01(c.pressure / 20.0f);
    Color lowPress = Color{200, 230, 255, 40};  // Faint, transparent sky
    Color highPress = Color{40, 100, 160, 180}; // Dense, heavy atmosphere
    Color baseCol = LerpColor(lowPress, highPress, pT);

    // Temperature visualization: distinct from fire/magma
    if (c.temperature < 15.0f) {
      // Cold air: Frosty cyan tint
      float tT = Clamp01((15.0f - c.temperature) / 50.0f); // 15C to -35C
      return LerpColor(baseCol, Color{130, 255, 255, 255}, tT * 0.4f);
    } else if (c.temperature > 35.0f) {
      // Hot air: Warm purple/magenta haze (avoids looking like fire)
      float tT = Clamp01((c.temperature - 35.0f) / 400.0f); // 35C to 435C
      return LerpColor(baseCol, Color{255, 120, 255, 255}, tT * 0.4f);
    }
    return baseCol;
  }
  case Element::WATER: {
    // High pressure water (deep/compressed) turns dark oceanic blue
    float t = Clamp01(c.pressure / 20.0f);
    Color lowPress = Color{40, 140, 220, 210}; // Vibrant surface water
    Color highPress = Color{10, 30, 90, 245};  // Deep, heavy abyss blue
    return LerpColor(lowPress, highPress, t);
  }
  case Element::EARTH: {
    // Earth turns into glowing magma under extreme heat
    float heatT = Clamp01((c.temperature - 20.0f) / 300.0f);
    Color coldEarth = Color{90, 55, 30, 255}; // Rich, dark soil brown
    Color hotEarth = Color{230, 60, 10, 255}; // Glowing magma orange-red
    return LerpColor(coldEarth, hotEarth, heatT);
  }
  case Element::FIRE: {
    // Standard fire gradient from a deep ember to a hot white-yellow spark
    float t = Clamp01((c.temperature - 200.0f) / 600.0f);
    Color coolFire = Color{210, 30, 0, 255};   // Deep crimson
    Color hotFire = Color{255, 240, 150, 255}; // Brilliant bright yellow-white
    return LerpColor(coolFire, hotFire, t);
  }
  case Element::STEAM: {
    // Denser/hotter steam is whiter and more opaque; cool steam dissipates
    float t = Clamp01((c.temperature - 90.0f) / 60.0f);
    Color coolSteam = Color{160, 160, 170, 60}; // Fading, thin vapor
    Color hotSteam = Color{240, 240, 245, 180}; // Thick, energetic steam
    return LerpColor(coolSteam, hotSteam, t);
  }
  case Element::CLOUD: {
    // High moisture turns a fluffy white cloud into a dark, heavy storm cloud
    float m = Clamp01(c.moisture);
    Color dryCloud = Color{240, 240, 245, 220}; // Fluffy white cloud
    Color wetCloud = Color{70, 80, 95, 240};    // Dark, ominous rain-cloud
    return LerpColor(dryCloud, wetCloud, m);
  }
  case Element::ICE: {
    // Colder ice looks deep and frozen; melting ice looks bright and wet
    float t = Clamp01((c.temperature + 40.0f) / 40.0f); // Range: -40C to 0C
    Color deepCold = Color{100, 180, 240, 255};         // Deep glacial blue
    Color melting = Color{230, 245, 255, 230}; // Bright, frosty white-blue
    return LerpColor(deepCold, melting, t);
  }
  case Element::SAND: {
    return Color{220, 180, 100, 255}; // Sand yellow
  }
  case Element::ROCK: {
    return Color{100, 100, 100, 255}; // Rock gray
  }
  case Element::WOOD: {
    return Color{120, 78, 40, 255}; // Warm bark brown
  }
  case Element::GRASS: {
    return Color{70, 150, 55, 255}; // Meadow green
  }
  case Element::SMOKE: {
    // Thins out as it ages
    float t = Clamp01(c.lifetime / 3.0f);
    return Color{60, 58, 62, (unsigned char)(40 + 120 * t)};
  }
  default:
    return BLACK;
  }
}
