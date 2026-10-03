#include "whas/ui/editor_icons.h"
#include "whas/engine/view.h"
#include "whas/ui/theme.h"
#include "whas/ui/widgets.h"
#include <cmath>

void DrawEditorIcon(ImDrawList *dl, EditorIcon icon, ImVec2 c, float size,
                    ImU32 col) {
  float h = size * 0.5f, t = std::max(1.5f, size * 0.08f);
  auto P = [&](float x, float y) { return ImVec2{c.x + x * h, c.y + y * h}; };
  switch (icon) {
  case EditorIcon::Brush:
    dl->AddLine(P(0.8f, -0.8f), P(-0.2f, 0.2f), col, t * 1.4f);
    dl->AddCircleFilled(P(-0.45f, 0.45f), h * 0.35f, col);
    break;
  case EditorIcon::Select:
    dl->AddTriangleFilled(P(-0.6f, -0.8f), P(-0.6f, 0.6f), P(0.5f, 0.2f), col);
    dl->AddLine(P(0.0f, 0.3f), P(0.5f, 0.9f), col, t * 1.5f);
    break;
  case EditorIcon::Start:
    dl->AddLine(P(-0.5f, 0.9f), P(-0.5f, -0.9f), col, t);
    dl->AddTriangleFilled(P(-0.5f, -0.9f), P(0.7f, -0.55f), P(-0.5f, -0.2f), col);
    break;
  case EditorIcon::Gate:
    dl->AddRect(P(-0.5f, -0.3f), P(0.5f, 0.9f), col, 0, 0, t);
    dl->PathArcTo(P(0.0f, -0.3f), h * 0.5f, 3.14159f, 6.28318f, 12);
    dl->PathStroke(col, 0, t);
    dl->AddLine(P(0.0f, -0.8f), P(0.0f, 0.9f), col, t * 0.8f);
    dl->AddLine(P(-0.5f, 0.25f), P(0.5f, 0.25f), col, t * 0.8f);
    break;
  case EditorIcon::Bench:
    dl->AddRectFilled(P(-0.8f, -0.2f), P(0.8f, 0.05f), col);
    dl->AddLine(P(-0.6f, 0.05f), P(-0.6f, 0.8f), col, t);
    dl->AddLine(P(0.6f, 0.05f), P(0.6f, 0.8f), col, t);
    break;
  case EditorIcon::Shrine:
    dl->AddRectFilled(P(-0.3f, 0.3f), P(0.3f, 0.9f), col);
    dl->AddCircle(P(0.0f, -0.35f), h * 0.45f, col, 16, t);
    break;
  case EditorIcon::Mage:
    dl->AddTriangleFilled(P(0.0f, -0.95f), P(-0.7f, 0.2f), P(0.7f, 0.2f), col);
    dl->AddLine(P(-0.9f, 0.25f), P(0.9f, 0.25f), col, t * 1.4f);
    dl->AddCircle(P(0.0f, 0.6f), h * 0.3f, col, 12, t);
    break;
  case EditorIcon::Undead:
    dl->AddCircle(P(0.0f, -0.15f), h * 0.65f, col, 16, t);
    dl->AddCircleFilled(P(-0.25f, -0.2f), h * 0.15f, col);
    dl->AddCircleFilled(P(0.25f, -0.2f), h * 0.15f, col);
    dl->AddLine(P(-0.3f, 0.8f), P(0.3f, 0.8f), col, t);
    break;
  case EditorIcon::Flyer:
    dl->AddLine(P(0.0f, 0.2f), P(-0.9f, -0.5f), col, t * 1.3f);
    dl->AddLine(P(0.0f, 0.2f), P(0.9f, -0.5f), col, t * 1.3f);
    dl->AddCircleFilled(P(0.0f, 0.25f), h * 0.25f, col);
    break;
  case EditorIcon::Npc:
    dl->AddCircleFilled(P(0.0f, -0.5f), h * 0.32f, col);
    dl->AddTriangleFilled(P(0.0f, -0.15f), P(-0.6f, 0.9f), P(0.6f, 0.9f), col);
    break;
  case EditorIcon::Region:
    for (int i = 0; i < 4; ++i) {
      float a = -0.8f + i * 0.45f;
      dl->AddLine(P(a, -0.7f), P(a + 0.25f, -0.7f), col, t);
      dl->AddLine(P(a, 0.7f), P(a + 0.25f, 0.7f), col, t);
    }
    dl->AddLine(P(-0.8f, -0.7f), P(-0.8f, 0.7f), col, t);
    dl->AddLine(P(0.8f, -0.7f), P(0.8f, 0.7f), col, t);
    break;
  case EditorIcon::Settle:
    for (int i = 0; i < 3; ++i) {
      float y = -0.5f + i * 0.5f;
      dl->AddBezierCubic(P(-0.8f, y), P(-0.3f, y - 0.3f), P(0.3f, y + 0.3f),
                         P(0.8f, y), col, t);
    }
    break;
  }
}

void WrapToolbar(float width) {
  if (ImGui::GetContentRegionAvail().x < width)
    ImGui::NewLine();
}

bool IconButton(const char *id, EditorIcon icon, bool selected,
                const char *tooltip) {
  float s = View::UiScale();
  ImVec2 size{36 * s, 36 * s};
  WrapToolbar(size.x);
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool pressed = Widgets::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  using Theme::Tone;
  dl->AddRectFilled(p, {p.x + size.x, p.y + size.y},
                    Theme::U32(selected ? Tone::Brass
                                        : (hovered ? Tone::UmberHi : Tone::Umber)),
                    4 * s);
  dl->AddRect(p, {p.x + size.x, p.y + size.y},
              Theme::U32(selected ? Tone::Oxblood : Tone::BrassDim), 4 * s, 0,
              selected ? 2.0f * s : 1.0f);
  DrawEditorIcon(dl, icon, {p.x + size.x * 0.5f, p.y + size.y * 0.5f},
                 size.x * 0.62f, Theme::U32(selected ? Tone::Ink : Tone::Parchment));
  if (hovered && tooltip)
    ImGui::SetTooltip("%s", tooltip);
  return pressed;
}
