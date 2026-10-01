#include "whas/ui/theme.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Theme {

namespace {

constexpr const char *BODY_FONT = "assets/fonts/EBGaramond.ttf";
constexpr const char *HEADING_FONT = "assets/fonts/Cinzel.ttf";
// raylib fonts are rasterised once this big and scaled down
constexpr int RL_FONT_PX = 96;

ImFont *g_heading = nullptr;
ImFont *g_body = nullptr;
Font g_rlHeading{};
Font g_rlBody{};
bool g_rlLoaded = false;

struct Rgb {
  unsigned char r, g, b;
};

Rgb Palette(Tone tone) {
  switch (tone) {
  case Tone::Ink:
    return {13, 11, 9};
  case Tone::Soot:
    return {24, 20, 16};
  case Tone::Umber:
    return {38, 32, 25};
  case Tone::UmberHi:
    return {52, 44, 33};
  case Tone::Line:
    return {64, 54, 40};
  case Tone::Parchment:
    return {230, 220, 195};
  case Tone::Muted:
    return {168, 154, 124};
  case Tone::Faint:
    return {110, 100, 82};
  case Tone::Brass:
    return {184, 148, 90};
  case Tone::BrassBright:
    return {222, 190, 125};
  case Tone::BrassDim:
    return {112, 90, 56};
  case Tone::Verdigris:
    return {111, 154, 139};
  case Tone::Oxblood:
    return {150, 62, 52};
  case Tone::Moss:
    return {128, 144, 92};
  }
  return {255, 0, 255};
}

ImFont *AddFont(const char *path, float size) {
  ImGuiIO &io = ImGui::GetIO();
  if (!std::filesystem::exists(path))
    return io.Fonts->AddFontDefault();
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  return io.Fonts->AddFontFromFileTTF(path, size, &config);
}

Font LoadRl(const char *path) {
  if (!std::filesystem::exists(path))
    return GetFontDefault();
  Font font = LoadFontEx(path, RL_FONT_PX, nullptr, 0);
  GenTextureMipmaps(&font.texture);
  SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
  return font;
}

} // namespace

ImVec4 Vec(Tone tone, float alpha) {
  Rgb c = Palette(tone);
  return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

ImU32 U32(Tone tone, float alpha) { return ImGui::GetColorU32(Vec(tone, alpha)); }

Color Rl(Tone tone, float alpha) {
  Rgb c = Palette(tone);
  return {c.r, c.g, c.b,
          static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255)};
}

void LoadImGuiFonts() {
  // The first font added is ImGui's default
  g_body = AddFont(BODY_FONT, BODY_SIZE);
  g_heading = AddFont(HEADING_FONT, HEADING_SIZE);
}

ImFont *Heading() { return g_heading ? g_heading : ImGui::GetFont(); }
ImFont *Body() { return g_body ? g_body : ImGui::GetFont(); }

void Apply(ImGuiStyle &s) {
  s.WindowPadding = {14, 12};
  s.FramePadding = {9, 5};
  s.ItemSpacing = {9, 7};
  s.ItemInnerSpacing = {7, 5};
  s.IndentSpacing = 18;
  s.ScrollbarSize = 12;
  s.GrabMinSize = 10;
  s.WindowBorderSize = 1;
  s.ChildBorderSize = 1;
  s.PopupBorderSize = 1;
  s.FrameBorderSize = 1;
  s.TabBorderSize = 0;
  s.WindowRounding = 2;
  s.ChildRounding = 2;
  s.FrameRounding = 1;
  s.PopupRounding = 2;
  s.ScrollbarRounding = 1;
  s.GrabRounding = 1;
  s.TabRounding = 1;
  s.WindowTitleAlign = {0.5f, 0.5f};
  s.SeparatorTextAlign = {0.5f, 0.5f};
  s.SeparatorTextBorderSize = 1;
  s.DisabledAlpha = 0.45f;

  ImVec4 *c = s.Colors;
  c[ImGuiCol_Text] = Vec(Tone::Parchment);
  c[ImGuiCol_TextDisabled] = Vec(Tone::Faint);
  c[ImGuiCol_WindowBg] = Vec(Tone::Soot, 0.97f);
  c[ImGuiCol_ChildBg] = Vec(Tone::Ink, 0.35f);
  c[ImGuiCol_PopupBg] = Vec(Tone::Soot, 0.98f);
  c[ImGuiCol_Border] = Vec(Tone::BrassDim, 0.7f);
  c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  c[ImGuiCol_FrameBg] = Vec(Tone::Ink, 0.85f);
  c[ImGuiCol_FrameBgHovered] = Vec(Tone::Umber);
  c[ImGuiCol_FrameBgActive] = Vec(Tone::UmberHi);
  c[ImGuiCol_TitleBg] = Vec(Tone::Ink);
  c[ImGuiCol_TitleBgActive] = Vec(Tone::Umber);
  c[ImGuiCol_TitleBgCollapsed] = Vec(Tone::Ink, 0.8f);
  c[ImGuiCol_MenuBarBg] = Vec(Tone::Ink);
  c[ImGuiCol_ScrollbarBg] = Vec(Tone::Ink, 0.4f);
  c[ImGuiCol_ScrollbarGrab] = Vec(Tone::Line);
  c[ImGuiCol_ScrollbarGrabHovered] = Vec(Tone::BrassDim);
  c[ImGuiCol_ScrollbarGrabActive] = Vec(Tone::Brass);
  c[ImGuiCol_CheckMark] = Vec(Tone::BrassBright);
  c[ImGuiCol_SliderGrab] = Vec(Tone::Brass);
  c[ImGuiCol_SliderGrabActive] = Vec(Tone::BrassBright);
  c[ImGuiCol_Button] = Vec(Tone::Umber);
  c[ImGuiCol_ButtonHovered] = Vec(Tone::UmberHi);
  c[ImGuiCol_ButtonActive] = Vec(Tone::BrassDim);
  c[ImGuiCol_Header] = Vec(Tone::Umber);
  c[ImGuiCol_HeaderHovered] = Vec(Tone::UmberHi);
  c[ImGuiCol_HeaderActive] = Vec(Tone::BrassDim);
  c[ImGuiCol_Separator] = Vec(Tone::Line);
  c[ImGuiCol_SeparatorHovered] = Vec(Tone::BrassDim);
  c[ImGuiCol_SeparatorActive] = Vec(Tone::Brass);
  c[ImGuiCol_ResizeGrip] = Vec(Tone::BrassDim, 0.3f);
  c[ImGuiCol_ResizeGripHovered] = Vec(Tone::Brass, 0.6f);
  c[ImGuiCol_ResizeGripActive] = Vec(Tone::BrassBright, 0.9f);
  c[ImGuiCol_Tab] = Vec(Tone::Ink);
  c[ImGuiCol_TabHovered] = Vec(Tone::UmberHi);
  c[ImGuiCol_TabSelected] = Vec(Tone::Umber);
  c[ImGuiCol_TabSelectedOverline] = Vec(Tone::Brass);
  c[ImGuiCol_TabDimmed] = Vec(Tone::Ink);
  c[ImGuiCol_TabDimmedSelected] = Vec(Tone::Umber);
  c[ImGuiCol_TabDimmedSelectedOverline] = Vec(Tone::BrassDim);
  c[ImGuiCol_PlotLines] = Vec(Tone::Brass);
  c[ImGuiCol_PlotLinesHovered] = Vec(Tone::BrassBright);
  c[ImGuiCol_PlotHistogram] = Vec(Tone::Brass);
  c[ImGuiCol_PlotHistogramHovered] = Vec(Tone::BrassBright);
  c[ImGuiCol_TableHeaderBg] = Vec(Tone::Umber);
  c[ImGuiCol_TableBorderStrong] = Vec(Tone::Line);
  c[ImGuiCol_TableBorderLight] = Vec(Tone::Line, 0.5f);
  c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
  c[ImGuiCol_TableRowBgAlt] = Vec(Tone::Parchment, 0.03f);
  c[ImGuiCol_TextLink] = Vec(Tone::BrassBright);
  c[ImGuiCol_TextSelectedBg] = Vec(Tone::Verdigris, 0.35f);
  c[ImGuiCol_DragDropTarget] = Vec(Tone::BrassBright);
  c[ImGuiCol_NavCursor] = Vec(Tone::Brass);
  c[ImGuiCol_NavWindowingHighlight] = Vec(Tone::Parchment, 0.7f);
  c[ImGuiCol_NavWindowingDimBg] = Vec(Tone::Ink, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = Vec(Tone::Ink, 0.6f);
}

void LoadRaylibFonts() {
  if (g_rlLoaded)
    return;
  g_rlHeading = LoadRl(HEADING_FONT);
  g_rlBody = LoadRl(BODY_FONT);
  g_rlLoaded = true;
}

void UnloadRaylibFonts() {
  if (!g_rlLoaded)
    return;
  if (g_rlHeading.texture.id != GetFontDefault().texture.id)
    UnloadFont(g_rlHeading);
  if (g_rlBody.texture.id != GetFontDefault().texture.id)
    UnloadFont(g_rlBody);
  g_rlLoaded = false;
}

const Font &RlHeading() {
  LoadRaylibFonts();
  return g_rlHeading;
}

const Font &RlBody() {
  LoadRaylibFonts();
  return g_rlBody;
}

void DrawText(const Font &font, const char *text, Vector2 pos, float size,
              Color color) {
  DrawTextEx(font, text, pos, size, size * 0.04f, color);
}

void DrawTextCentered(const Font &font, const char *text, float cx, float y,
                      float size, Color color) {
  Vector2 m = MeasureText(font, text, size);
  DrawText(font, text, {cx - m.x * 0.5f, y}, size, color);
}

Vector2 MeasureText(const Font &font, const char *text, float size) {
  return MeasureTextEx(font, text, size, size * 0.04f);
}

void Diamond(ImDrawList *dl, ImVec2 c, float r, ImU32 col) {
  dl->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r},
                    {c.x - r, c.y}, col);
}

void Plate(ImDrawList *dl, ImVec2 a, ImVec2 b, bool hot, bool pressed,
           bool selected) {
  float notch = std::min(5.0f, std::min(b.x - a.x, b.y - a.y) * 0.25f);
  // Notched corners: an octagon-ish plate
  auto plate = [&](ImVec2 p, ImVec2 q, float n) {
    dl->PathLineTo({p.x + n, p.y});
    dl->PathLineTo({q.x - n, p.y});
    dl->PathLineTo({q.x, p.y + n});
    dl->PathLineTo({q.x, q.y - n});
    dl->PathLineTo({q.x - n, q.y});
    dl->PathLineTo({p.x + n, q.y});
    dl->PathLineTo({p.x, q.y - n});
    dl->PathLineTo({p.x, p.y + n});
  };
  ImU32 top = U32(pressed ? Tone::Soot : hot ? Tone::UmberHi : Tone::Umber);
  ImU32 bottom = U32(pressed ? Tone::Umber : Tone::Soot);
  plate(a, b, notch);
  dl->PathFillConvex(top);
  // A lamp-lit sheen on the upper half
  dl->AddRectFilledMultiColor({a.x + notch, a.y + 1}, {b.x - notch, (a.y + b.y) * 0.5f},
                              U32(Tone::Parchment, hot ? 0.06f : 0.03f),
                              U32(Tone::Parchment, hot ? 0.06f : 0.03f),
                              U32(Tone::Parchment, 0.0f), U32(Tone::Parchment, 0.0f));
  dl->AddRectFilledMultiColor({a.x + notch, (a.y + b.y) * 0.5f}, {b.x - notch, b.y - 1},
                              U32(Tone::Ink, 0.0f), U32(Tone::Ink, 0.0f), bottom, bottom);
  Tone trim = selected ? Tone::BrassBright : hot ? Tone::Brass : Tone::BrassDim;
  plate(a, b, notch);
  dl->PathStroke(U32(trim), ImDrawFlags_Closed, selected ? 1.6f : 1.0f);
  // Inner engraved line
  plate({a.x + 3, a.y + 3}, {b.x - 3, b.y - 3}, std::max(1.0f, notch - 2));
  dl->PathStroke(U32(Tone::Ink, pressed ? 0.9f : 0.6f), ImDrawFlags_Closed, 1.0f);
  if (selected) {
    Diamond(dl, {a.x, (a.y + b.y) * 0.5f}, 3.0f, U32(Tone::BrassBright));
    Diamond(dl, {b.x, (a.y + b.y) * 0.5f}, 3.0f, U32(Tone::BrassBright));
  }
}

void Rule(ImDrawList *dl, ImVec2 a, float width, ImU32 col) {
  ImVec2 mid{a.x + width * 0.5f, a.y};
  ImU32 clear = col & ~IM_COL32_A_MASK;
  dl->AddRectFilledMultiColor({a.x, a.y - 0.5f}, {mid.x - 6, a.y + 0.5f}, clear,
                              col, col, clear);
  dl->AddRectFilledMultiColor({mid.x + 6, a.y - 0.5f}, {a.x + width, a.y + 0.5f},
                              col, clear, clear, col);
  Diamond(dl, mid, 3.5f, col);
}

void Seal(ImDrawList *dl, ImVec2 c, float r, ImU32 col, float spin) {
  dl->AddCircle(c, r, col, 64, 1.5f);
  dl->AddCircle(c, r * 0.86f, col, 64, 1.0f);
  dl->AddCircle(c, r * 0.36f, col, 48, 1.0f);
  // Ticks between the outer rings
  for (int i = 0; i < 48; ++i) {
    float a = spin + i * 2.0f * PI / 48;
    float in = i % 4 == 0 ? r * 0.80f : r * 0.86f;
    dl->AddLine({c.x + std::cos(a) * in, c.y + std::sin(a) * in},
                {c.x + std::cos(a) * r, c.y + std::sin(a) * r}, col, 1.0f);
  }
  // An eight-pointed star of two squares
  for (int sq = 0; sq < 2; ++sq) {
    for (int i = 0; i < 4; ++i) {
      float a = -spin * 0.5f + sq * PI / 4 + i * PI / 2;
      dl->PathLineTo({c.x + std::cos(a) * r * 0.78f, c.y + std::sin(a) * r * 0.78f});
    }
    dl->PathStroke(col, ImDrawFlags_Closed, 1.2f);
  }
  Diamond(dl, c, r * 0.12f, col);
}

} // namespace Theme
