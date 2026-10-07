#include "whas/ui/terrain_brush.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/map.h"
#include "whas/ui/editor_icons.h"
#include "whas/ui/theme.h"
#include "whas/ui/widgets.h"
#include <algorithm>

namespace {

// Keys 1-9, in palette order; the eraser is E
constexpr Element kPalette[] = {Element::EARTH, Element::ROCK,  Element::SAND,
                                Element::GRASS, Element::WOOD,  Element::WATER,
                                Element::ICE,   Element::CLOUD, Element::FIRE};
constexpr int UNDO_STEPS = 20;

const char *Label(Element e) {
  switch (e) {
  case Element::AIR:
    return "Eraser";
  case Element::EARTH:
    return "Earth";
  case Element::ROCK:
    return "Rock";
  case Element::SAND:
    return "Sand";
  case Element::GRASS:
    return "Grass";
  case Element::WOOD:
    return "Wood";
  case Element::WATER:
    return "Water";
  case Element::ICE:
    return "Ice";
  case Element::CLOUD:
    return "Cloud";
  case Element::FIRE:
    return "Fire";
  default:
    return ElementName(e);
  }
}

} // namespace

Color TerrainBrush::Swatch(Element e) {
  switch (e) {
  case Element::EARTH:
    return {90, 55, 30, 255};
  case Element::ROCK:
    return {100, 100, 100, 255};
  case Element::SAND:
    return {220, 180, 100, 255};
  case Element::GRASS:
    return {70, 150, 55, 255};
  case Element::WOOD:
    return {120, 78, 40, 255};
  case Element::WATER:
    return {40, 140, 220, 255};
  case Element::ICE:
    return {150, 210, 245, 255};
  case Element::CLOUD:
    return {225, 225, 235, 255};
  case Element::FIRE:
    return {240, 110, 20, 255};
  default:
    return {245, 235, 210, 255}; // the eraser: bare paper
  }
}

void TerrainBrush::HandleKeys(Simulation &sim) {
  for (int i = 0; i < 9; ++i)
    if (IsKeyPressed(KEY_ONE + i))
      element = kPalette[i];
  bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
  if (IsKeyPressed(KEY_E) && !ctrl)
    element = Element::AIR;
  if (IsKeyPressed(KEY_LEFT_BRACKET))
    size = std::max(0, size - 1);
  if (IsKeyPressed(KEY_RIGHT_BRACKET))
    size = std::min(MAX_SIZE, size + 1);
  if (ctrl && IsKeyPressed(KEY_Z))
    Undo(sim);
}

// Rock is anchored as it will be when the map loads, so it doesn't tumble
// (and thump) while the editor lets the world settle
static void PaintAnchored(Simulation &sim, int cx, int cy, Element element, int size) {
  sim.Paint(cx, cy, element, size);
  if (element != Element::ROCK)
    return;
  for (int dy = -size; dy <= size; ++dy)
    for (int dx = -size; dx <= size; ++dx)
      if (dx * dx + dy * dy <= size * size)
        sim.Anchor(cx + dx, cy + dy);
}

void TerrainBrush::Paint(Simulation &sim, Vector2 cell, bool overUi) {
  bool left = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  bool right = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
  if (!left && !right) {
    m_stroke = false;
    return;
  }
  if (overUi && !m_stroke)
    return;
  if (!m_stroke) {
    m_stroke = true;
    m_undo.push_back(Maps::CaptureCells(sim));
    if (m_undo.size() > UNDO_STEPS)
      m_undo.erase(m_undo.begin());
  }
  int cx = static_cast<int>(cell.x), cy = static_cast<int>(cell.y);
  if (right || element == Element::AIR)
    sim.Erase(cx, cy, size);
  else
    PaintAnchored(sim, cx, cy, element, size);
  if (!overUi) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0)
      size = std::clamp(size + static_cast<int>(wheel), 0, MAX_SIZE);
  }
}

void TerrainBrush::Undo(Simulation &sim) {
  if (m_undo.empty())
    return;
  std::vector<uint8_t> before = std::move(m_undo.back());
  m_undo.pop_back();
  std::vector<uint8_t> now = Maps::CaptureCells(sim);
  for (int y = 0; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x) {
      size_t i = static_cast<size_t>(y) * GRID_W + x;
      if (before[i] == now[i])
        continue;
      sim.Erase(x, y, 0);
      if (before[i] != static_cast<uint8_t>(Element::AIR))
        PaintAnchored(sim, x, y, static_cast<Element>(before[i]), 0);
    }
}

void TerrainBrush::DrawPalette() {
  float s = View::UiScale();
  float box = 30 * s;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  auto swatch = [&](Element e, const char *key) {
    ImGui::PushID(static_cast<int>(e));
    WrapToolbar(box);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool picked = Widgets::InvisibleButton("##sw", {box, box});
    bool hovered = ImGui::IsItemHovered();
    Color c = Swatch(e);
    dl->AddRectFilled(p, {p.x + box, p.y + box}, IM_COL32(c.r, c.g, c.b, 255), 3 * s);
    if (e == Element::AIR) // the eraser: a struck-through box
      dl->AddLine({p.x + 5 * s, p.y + box - 5 * s}, {p.x + box - 5 * s, p.y + 5 * s},
                  IM_COL32(150, 40, 30, 255), 2.5f * s);
    bool selected = element == e;
    dl->AddRect({p.x - 1, p.y - 1}, {p.x + box + 1, p.y + box + 1},
                selected ? Theme::U32(Theme::Tone::Oxblood) : Theme::U32(Theme::Tone::Ink, 0.6f),
                3 * s, 0, selected ? 3.0f * s : 1.0f);
    dl->AddText({p.x + 3 * s, p.y + 1 * s}, IM_COL32(255, 255, 255, 220), key);
    if (hovered)
      ImGui::SetTooltip("%s  (%s)", Label(e), key);
    if (picked)
      element = e;
    ImGui::PopID();
    ImGui::SameLine(0, 4 * s);
  };
  for (int i = 0; i < 9; ++i) {
    char key[2] = {static_cast<char>('1' + i), 0};
    swatch(kPalette[i], key);
  }
  swatch(Element::AIR, "E");
  ImGui::SameLine(0, 12 * s);
  WrapToolbar(110 * s);
  ImGui::SetNextItemWidth(110 * s);
  ImGui::SliderInt("##size", &size, 0, MAX_SIZE, "size %d");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Brush size: [ and ] or the mouse wheel");
}

void TerrainBrush::DrawCursor(Vector2 cell) const {
  Color c = Swatch(element);
  float r = (size + 0.5f) * CELL_SIZE;
  Vector2 at{cell.x * CELL_SIZE, cell.y * CELL_SIZE};
  DrawCircleV(at, r, Color{c.r, c.g, c.b, 50});
  DrawCircleLinesV(at, r, Color{c.r, c.g, c.b, 230});
  DrawCircleLinesV(at, r + 1.5f, Color{30, 20, 10, 160});
}
