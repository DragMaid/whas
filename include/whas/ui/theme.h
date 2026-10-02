#pragma once
#include "imgui.h"
#include <raylib.h>

// The look of every screen: soot and ink panels, parchment text, aged brass
// trim with a little verdigris, oxblood for danger. Muted on purpose, like
// an old grimoire under lamplight. Headings are set in Cinzel, text in EB
// Garamond (both SIL OFL, assets/fonts).
namespace Theme {

enum class Tone {
  Ink,         // deepest background
  Soot,        // panels
  Umber,       // raised surfaces, buttons
  UmberHi,     // hovered surfaces
  Line,        // quiet borders and rules
  Parchment,   // body text
  Muted,       // secondary text
  Faint,       // disabled text, hints
  Brass,       // trim, accents
  BrassBright, // hovered trim, highlights
  BrassDim,    // resting trim
  Verdigris,   // selection, "your turn"
  Oxblood,     // danger, damage, errors
  Moss,        // success
};

ImVec4 Vec(Tone tone, float alpha = 1.0f);
// Draw-list colour; follows ImGui's style alpha (so BeginDisabled fades it)
ImU32 U32(Tone tone, float alpha = 1.0f);
Color Rl(Tone tone, float alpha = 1.0f);

// rlImGui font callback: loads the body font (default) and the heading font
void LoadImGuiFonts();
// The ImGui style; call once after setup
void Apply(ImGuiStyle &style);
ImFont *Heading();
ImFont *Body();
// Base sizes at UI scale 1 (720p)
constexpr float BODY_SIZE = 19.0f;
constexpr float HEADING_SIZE = 20.0f;

// Fonts for text drawn by raylib (HUD, banners). Needs the window.
void LoadRaylibFonts();
void UnloadRaylibFonts();
const Font &RlHeading();
const Font &RlBody();
// raylib text in the theme fonts, measured and drawn at `size` pixels
void DrawText(const Font &font, const char *text, Vector2 pos, float size,
              Color color);
// Centred on x
void DrawTextCentered(const Font &font, const char *text, float cx, float y,
                      float size, Color color);
Vector2 MeasureText(const Font &font, const char *text, float size);

// --- Ornament, on an ImGui draw list ---

// A small filled diamond
void Diamond(ImDrawList *dl, ImVec2 c, float r, ImU32 col);
// An engraved plate: dark fill, double rule (outer brass, inner shadow) and
// little notched corners. hot = hovered or selected.
void Plate(ImDrawList *dl, ImVec2 a, ImVec2 b, bool hot, bool pressed,
           bool selected = false);
// A horizontal rule with a diamond in the middle and fading ends
void Rule(ImDrawList *dl, ImVec2 a, float width, ImU32 col);
// A sigil-like ring for crests and empty states: rings, a star and ticks
void Seal(ImDrawList *dl, ImVec2 c, float r, ImU32 col, float spin = 0.0f);

} // namespace Theme
