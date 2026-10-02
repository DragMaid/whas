#include "whas/ui/widgets.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

using Theme::Tone;

namespace Widgets {

namespace {

// Where a label stops being shown ("Save##spell" shows "Save")
const char *LabelEnd(const char *label) {
  const char *hash = std::strstr(label, "##");
  return hash ? hash : label + std::strlen(label);
}

float ButtonFontSize(bool small) {
  return ImGui::GetFontSize() * (small ? 0.74f : 0.84f);
}

ImVec2 LabelSize(const char *label, float fontSize) {
  return Theme::Heading()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label,
                                         LabelEnd(label));
}

bool PlateButton(const char *label, ImVec2 size, bool selected, bool small) {
  const ImGuiStyle &style = ImGui::GetStyle();
  float fontSize = ButtonFontSize(small);
  ImVec2 text = LabelSize(label, fontSize);
  ImVec2 pad = small ? ImVec2{style.FramePadding.x * 0.8f, style.FramePadding.y * 0.5f}
                     : ImVec2{style.FramePadding.x * 1.6f, style.FramePadding.y};
  float avail = ImGui::GetContentRegionAvail().x;
  if (size.x == 0)
    size.x = text.x + pad.x * 2;
  else if (size.x < 0)
    size.x = std::max(4.0f, avail + size.x + 1);
  if (size.y == 0)
    size.y = small ? fontSize + pad.y * 2 + 2 : ImGui::GetFrameHeight();

  ImVec2 a = ImGui::GetCursorScreenPos();
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hovered = ImGui::IsItemHovered();
  bool held = ImGui::IsItemActive();
  ImVec2 b{a.x + size.x, a.y + size.y};

  ImDrawList *dl = ImGui::GetWindowDrawList();
  Theme::Plate(dl, a, b, hovered || selected, held, selected);
  float press = held ? 1.0f : 0.0f;
  ImVec2 at{a.x + (size.x - text.x) * 0.5f,
            a.y + (size.y - text.y) * 0.5f + press};
  // Engraved: a dark shadow under the lettering
  dl->AddText(Theme::Heading(), fontSize, {at.x, at.y + 1},
              Theme::U32(Tone::Ink, 0.8f), label, LabelEnd(label));
  Tone ink = selected  ? Tone::BrassBright
             : hovered ? Tone::Parchment
                       : Tone::Muted;
  dl->AddText(Theme::Heading(), fontSize, at, Theme::U32(ink), label,
              LabelEnd(label));
  if (pressed)
    Click();
  return pressed;
}

} // namespace

bool Button(const char *label, const ImVec2 &size, bool selected) {
  return PlateButton(label, size, selected, false);
}

bool SmallButton(const char *label, bool selected) {
  return PlateButton(label, {0, 0}, selected, true);
}

bool Toggle(const char *label, bool *value) {
  float h = ImGui::GetFrameHeight();
  ImVec2 a = ImGui::GetCursorScreenPos();
  ImVec2 text = ImGui::CalcTextSize(label, nullptr, true);
  ImGui::PushID(label);
  bool pressed = ImGui::InvisibleButton("##toggle", {h + 6 + text.x, h});
  ImGui::PopID();
  bool hovered = ImGui::IsItemHovered();
  if (pressed) {
    *value = !*value;
    Click();
  }
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 c{a.x + h * 0.5f, a.y + h * 0.5f};
  float r = h * 0.32f;
  dl->AddQuad({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y},
              Theme::U32(hovered ? Tone::Brass : Tone::BrassDim), 1.2f);
  if (*value)
    Theme::Diamond(dl, c, r * 0.6f, Theme::U32(Tone::BrassBright));
  dl->AddText({a.x + h + 6, a.y + (h - text.y) * 0.5f},
              Theme::U32(*value || hovered ? Tone::Parchment : Tone::Muted),
              label, LabelEnd(label));
  return pressed;
}

void SectionHeader(const char *text) {
  ImGui::Dummy({0, 2});
  float size = ImGui::GetFontSize() * 0.82f;
  ImVec2 a = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  ImVec2 ts = Theme::Heading()->CalcTextSizeA(size, FLT_MAX, 0, text);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddText(Theme::Heading(), size, a, Theme::U32(Tone::Brass), text);
  float ruleX = a.x + ts.x + 10;
  if (width - ts.x - 10 > 20) {
    float y = a.y + ts.y * 0.55f;
    ImU32 col = Theme::U32(Tone::BrassDim);
    dl->AddLine({ruleX, y}, {a.x + width - 8, y}, col, 1.0f);
    Theme::Diamond(dl, {a.x + width - 4, y}, 2.5f, col);
  }
  ImGui::Dummy({width, ts.y + 4});
}

void Title(const char *text) {
  float size = ImGui::GetFontSize() * 1.35f;
  ImVec2 a = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  ImVec2 ts = Theme::Heading()->CalcTextSizeA(size, FLT_MAX, 0, text);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddText(Theme::Heading(), size, {a.x, a.y + 1}, Theme::U32(Tone::Ink), text);
  dl->AddText(Theme::Heading(), size, a, Theme::U32(Tone::Parchment), text);
  Theme::Rule(dl, {a.x, a.y + ts.y + 6}, width, Theme::U32(Tone::BrassDim));
  ImGui::Dummy({width, ts.y + 14});
}

bool NavItem(const char *label, bool selected, float width) {
  float h = ImGui::GetFrameHeight() * 1.5f;
  ImVec2 a = ImGui::GetCursorScreenPos();
  bool pressed = ImGui::InvisibleButton(label, {width, h});
  bool hovered = ImGui::IsItemHovered();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 b{a.x + width, a.y + h};
  if (selected || hovered)
    dl->AddRectFilledMultiColor(a, b, Theme::U32(Tone::Brass, selected ? 0.16f : 0.07f),
                                Theme::U32(Tone::Brass, 0.0f), Theme::U32(Tone::Brass, 0.0f),
                                Theme::U32(Tone::Brass, selected ? 0.16f : 0.07f));
  if (selected) {
    dl->AddRectFilled(a, {a.x + 2, b.y}, Theme::U32(Tone::BrassBright));
    Theme::Diamond(dl, {a.x + 16, a.y + h * 0.5f}, 3.5f, Theme::U32(Tone::BrassBright));
  } else {
    Theme::Diamond(dl, {a.x + 16, a.y + h * 0.5f}, 2.0f,
                   Theme::U32(hovered ? Tone::Brass : Tone::Line));
  }
  float size = ImGui::GetFontSize() * 0.86f;
  ImVec2 ts = Theme::Heading()->CalcTextSizeA(size, FLT_MAX, 0, label, LabelEnd(label));
  dl->AddText(Theme::Heading(), size, {a.x + 30, a.y + (h - ts.y) * 0.5f},
              Theme::U32(selected  ? Tone::BrassBright
                         : hovered ? Tone::Parchment
                                   : Tone::Muted),
              label, LabelEnd(label));
  if (pressed)
    Click();
  return pressed;
}

} // namespace Widgets
