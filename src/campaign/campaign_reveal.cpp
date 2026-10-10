#include "imgui.h"
#include "whas/audio/audio_manager.h"
#include "whas/campaign/campaign_play.h"
#include "whas/engine/view.h"
#include "whas/spell/glyph_docs.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include <algorithm>
#include <cmath>

// A glyph just learned: the world waits while it is inked onto the screen
// stroke by stroke inside a ring of light, then named and explained.

using namespace Campaign;
using Theme::Tone;

namespace {

constexpr float INK_START = 0.35f; // the ring closes first
constexpr float INK_END = 1.9f;
constexpr float READY = 2.3f; // the words are in; a key puts it away

float Ease(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}

// Lines of `text` no wider than `width`, each centred on `cx`
float CenteredText(ImDrawList *dl, ImFont *font, float size, float cx, float y,
                   float width, ImU32 col, const char *text) {
  const char *end = text + std::strlen(text);
  while (text < end) {
    const char *cut = font->CalcWordWrapPosition(size, text, end, width);
    if (cut == text)
      cut = text + 1;
    const char *nl = std::find(text, cut, '\n');
    if (nl < cut)
      cut = nl;
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, cut);
    dl->AddText(font, size, {cx - ts.x * 0.5f, y}, col, text, cut);
    y += size * 1.25f;
    text = cut;
    while (text < end && (*text == ' ' || *text == '\n'))
      ++text;
  }
  return y;
}

} // namespace

void CampaignPlay::Learn(const std::string &glyph, bool sigil, std::string from) {
  m_reveals.push_back({glyph, sigil, std::move(from), 0.0f});
  if (AudioManager *audio = AudioManager::Instance())
    audio->PlayUi(UiSound::Draw);
}

void CampaignPlay::DrawReveal(UI &ui) {
  Reveal &r = m_reveals.front();
  float dt = GetFrameTime();
  float before = r.t;
  r.t += dt;
  if (before < INK_END && r.t >= INK_END)
    if (AudioManager *audio = AudioManager::Instance())
      audio->PlayUi(UiSound::SpellPlan);

  // A key or click hurries the ink along, then puts it away
  bool press = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_SPACE) ||
               IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) ||
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  if (press && r.t > 0.2f) {
    if (r.t < READY) {
      r.t = READY;
    } else {
      m_reveals.erase(m_reveals.begin());
      return;
    }
  }

  float s = View::UiScale();
  float t = r.t;
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 screen = ImGui::GetIO().DisplaySize;
  ImVec2 c{screen.x * 0.5f, screen.y * 0.40f};
  float fade = Ease(t / 0.3f);
  float R = std::min(120 * s, screen.y * 0.2f);

  dl->AddRectFilled({0, 0}, screen, Theme::U32(Tone::Ink, 0.78f * fade));
  // A soft glow behind, swelling as the ink lands
  float swell = Ease((t - INK_END) / 0.4f);
  for (int i = 8; i >= 1; --i)
    dl->AddCircleFilled(c, R * (0.6f + 0.18f * i) * (1.0f + 0.12f * swell),
                        IM_COL32(240, 200, 120, (int)(10 * fade)), 64);

  // Slow rays
  ImU32 ray = IM_COL32(240, 205, 130, (int)(28 * fade + 22 * swell));
  for (int i = 0; i < 12; ++i) {
    float a = i * 2.0f * PI / 12 + t * 0.15f;
    float w = 0.07f;
    float len = R * (1.8f + 0.2f * std::sin(t * 1.3f + i));
    dl->AddTriangleFilled(c, {c.x + std::cos(a - w) * len, c.y + std::sin(a - w) * len},
                          {c.x + std::cos(a + w) * len, c.y + std::sin(a + w) * len}, ray);
  }

  // The ring draws itself round, then a second, thinner one
  ImU32 brass = Theme::U32(Tone::BrassBright, fade);
  float ring = Ease(t / INK_START);
  dl->PathArcTo(c, R, -PI * 0.5f, -PI * 0.5f + 2 * PI * ring, 96);
  dl->PathStroke(brass, 0, 3.0f * s);
  if (ring >= 1.0f) {
    dl->PathArcTo(c, R * 0.9f, PI * 0.5f, PI * 0.5f + 2 * PI * Ease((t - INK_START) / 0.6f), 96);
    dl->PathStroke(Theme::U32(Tone::Brass, 0.6f * fade), 0, 1.5f * s);
  }

  // The glyph, inked along its length
  if (const SvgAsset *a = ui.Glyphs().FindById(r.glyph)) {
    float k = R * 1.15f / std::max(a->viewWidth, a->viewHeight);
    auto at = [&](Vector2 p) {
      return ImVec2{c.x + (p.x - a->viewWidth * 0.5f) * k, c.y + (p.y - a->viewHeight * 0.5f) * k};
    };
    float total = 0.0f;
    for (const LineSeg &seg : a->segments)
      total += std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
    float left = total * Ease((t - INK_START) / (INK_END - INK_START));
    ImU32 ink = IM_COL32(255, 236, 190, (int)(255 * fade));
    ImU32 halo = IM_COL32(255, 200, 110, (int)(60 * fade));
    ImVec2 nib{-1, -1};
    for (const LineSeg &seg : a->segments) {
      if (left <= 0.0f)
        break;
      float len = std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
      float f = len > 0 ? std::min(1.0f, left / len) : 1.0f;
      left -= len;
      Vector2 end{seg.a.x + (seg.b.x - seg.a.x) * f, seg.a.y + (seg.b.y - seg.a.y) * f};
      dl->AddLine(at(seg.a), at(end), halo, 9.0f * s);
      dl->AddLine(at(seg.a), at(end), ink, 3.5f * s);
      nib = at(end);
    }
    // A spark where the pen is
    if (t < INK_END && nib.x >= 0) {
      dl->AddCircleFilled(nib, 6 * s, IM_COL32(255, 245, 210, 200));
      dl->AddCircleFilled(nib, 12 * s, IM_COL32(255, 210, 120, 60));
    }
  }

  // When it's done: a flash ring and sparks thrown outward
  if (t >= INK_END) {
    float e = t - INK_END;
    float p = Ease(e / 0.7f);
    dl->AddCircle(c, R * (1.0f + p * 0.9f), IM_COL32(255, 230, 170, (int)(220 * (1 - p))), 96,
                  4.0f * s * (1 - p) + 1);
    for (int i = 0; i < 28 && e < 1.2f; ++i) {
      float a = i * 2.39996f; // golden angle
      float v = 0.7f + 0.6f * std::fmod(i * 0.618f, 1.0f);
      float d = R * (1.0f + v * Ease(e / 1.2f) * 1.2f);
      float alpha = 1.0f - e / 1.2f;
      dl->AddCircleFilled({c.x + std::cos(a) * d, c.y + std::sin(a) * d}, 2.5f * s,
                          IM_COL32(255, 220, 150, (int)(230 * alpha)));
    }
  }

  // The words
  GlyphDocs::Info info = GlyphDocs::Get(r.glyph);
  std::string name = info.name ? info.name : r.glyph;
  float words = Ease((t - INK_END + 0.2f) / 0.5f);
  if (words > 0.0f) {
    // A dark band under the words, so the rays don't wash them out
    float top = c.y + R * 1.12f;
    ImU32 clear = Theme::U32(Tone::Ink, 0.0f), dark = Theme::U32(Tone::Ink, 0.85f * words);
    dl->AddRectFilledMultiColor({0, top}, {screen.x, top + 40 * s}, clear, clear, dark, dark);
    dl->AddRectFilled({0, top + 40 * s}, screen, dark);
    float y = c.y + R * 1.25f + 8 * s * (1 - words);
    float wrap = std::min(560 * s, screen.x - 60 * s);
    std::string kicker = std::string(r.sigil ? "Sigil learned" : "Sign learned") +
                         (r.from.empty() ? "" : "  -  " + r.from);
    y = CenteredText(dl, Theme::Body(), Theme::BODY_SIZE * 0.85f * s, c.x, y, wrap,
                     Theme::U32(Tone::Brass, words), kicker.c_str());
    y = CenteredText(dl, Theme::Heading(), Theme::HEADING_SIZE * 2.0f * s, c.x, y + 2 * s,
                     wrap, Theme::U32(Tone::Parchment, words), name.c_str());
    if (info.text && info.text[0])
      y = CenteredText(dl, Theme::Body(), Theme::BODY_SIZE * s, c.x, y + 6 * s, wrap,
                       Theme::U32(Tone::Muted, words), info.text);
    if (auto lore = m_def.lore.find(r.glyph); lore != m_def.lore.end() && !lore->second.empty())
      y = CenteredText(dl, Theme::Body(), Theme::BODY_SIZE * s, c.x, y + 6 * s, wrap,
                       Theme::U32(Tone::Faint, words), lore->second.c_str());
    if (t >= READY) {
      float blink = 0.55f + 0.45f * std::sin((t - READY) * 4.0f);
      CenteredText(dl, Theme::Body(), Theme::BODY_SIZE * 0.9f * s, c.x, y + 14 * s, wrap,
                   Theme::U32(Tone::BrassBright, blink),
                   "Draw it at a workbench.   E to go on");
    }
  }
}
