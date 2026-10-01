#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/view.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_quant.h"
#include "whas/ui/ui.h"
#include "whas/ui/widgets.h"
#include <algorithm>
#include <cmath>

// The bottom bar shared by the sandbox and matches:
//   [time button] [Draw|Cast] [ 6 hotbar slots | materials ] [deck picker]

namespace {

// At UI scale 1 (720p); scaled where they're used
constexpr float BASE_PAD = 6.0f;
constexpr float BASE_DECK_W = 210.0f;
constexpr float BASE_TOGGLE_W = 58.0f;

float Px(float v) { return v * View::UiScale(); }

ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

ImU32 FromColor(Color c, unsigned char a = 255) {
  return IM_COL32(c.r, c.g, c.b, a);
}

// Two triangles meeting at the waist
void Hourglass(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  dl->AddTriangleFilled({c.x - s, c.y - s}, {c.x + s, c.y - s}, c, col);
  dl->AddTriangleFilled({c.x - s, c.y + s}, {c.x + s, c.y + s}, c, col);
  dl->AddLine({c.x - s - 2, c.y - s}, {c.x + s + 2, c.y - s}, col, 2.0f);
  dl->AddLine({c.x - s - 2, c.y + s}, {c.x + s + 2, c.y + s}, col, 2.0f);
}

void PlayIcon(ImDrawList *dl, ImVec2 c, float s, ImU32 col) {
  dl->AddTriangleFilled({c.x - s * 0.6f, c.y - s}, {c.x - s * 0.6f, c.y + s},
                        {c.x + s, c.y}, col);
}

void Arc(ImDrawList *dl, ImVec2 c, float r, float from01, float to01,
         ImU32 col, float thick) {
  if (to01 <= from01)
    return;
  constexpr float TOP = -PI * 0.5f;
  dl->PathArcTo(c, r, TOP + from01 * 2 * PI, TOP + to01 * 2 * PI, 40);
  dl->PathStroke(col, 0, thick);
}

} // namespace

float UI::BarHeight() { return Px(60); }
float UI::BarY() { return GetScreenHeight() - BarHeight(); }

void UI::DrawActionBar(UIState &state) {
  const float PAD = Px(BASE_PAD);
  const float DECK_W = Px(BASE_DECK_W);
  const float TOGGLE_W = Px(BASE_TOGGLE_W);
  const float BAR_Y = BarY();
  const float BAR_HEIGHT = BarHeight();
  const float WIDTH = static_cast<float>(GetScreenWidth());
  ImGui::SetNextWindowPos({0, BAR_Y});
  ImGui::SetNextWindowSize({WIDTH, BAR_HEIGHT});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.12f, 0.97f));
  ImGui::Begin("##ActionBar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddLine({0, BAR_Y}, {WIDTH, BAR_Y}, Rgba(60, 60, 80), 1.0f);

  float h = BAR_HEIGHT - 2 * PAD;
  float x = PAD + 2;
  DrawTimeButton(state, {x, BAR_Y + PAD}, h);
  x += h + PAD * 2;

  bool sandbox = !m_gameMode;
  if (sandbox) {
    // Draw | Cast toggle, stacked; Tab flips it
    float bh = (h - 4) * 0.5f;
    auto toggle = [&](const char *label, SandboxTool tool, float y) {
      bool on = state.tool == tool;
      ImGui::SetCursorScreenPos({x, y});
      ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.30f, 0.45f, 0.85f, 1)
                                                : ImVec4(0.16f, 0.16f, 0.22f, 1));
      if (Widgets::Button(label, {TOGGLE_W, bh}))
        state.tool = tool;
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s  (Tab to switch)",
                          tool == SandboxTool::Draw
                              ? "Paint the world: left paint, right erase"
                              : "Cast spells from your avatar");
    };
    toggle("Draw", SandboxTool::Draw, BAR_Y + PAD);
    toggle("Cast", SandboxTool::Cast, BAR_Y + PAD + bh + 4);
    x += TOGGLE_W + PAD * 2;
  }

  ImVec2 contentPos{x, BAR_Y + PAD};
  ImVec2 contentSize{WIDTH - x - DECK_W - PAD * 3, h};
  if (sandbox && state.tool == SandboxTool::Draw)
    DrawMaterials(state, contentPos, contentSize);
  else
    DrawHotbar(state, contentPos, contentSize);

  DrawDeckPicker(state, {WIDTH - DECK_W - PAD, BAR_Y + PAD},
                 {DECK_W, h});

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}

void UI::DrawTimeButton(UIState &state, ImVec2 pos, float size) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(pos);
  bool clickable = state.clock == ClockLook::Running ||
                   state.clock == ClockLook::Stopped ||
                   state.clock == ClockLook::Waiting;
  if (ImGui::InvisibleButton("##time", {size, size}) && clickable) {
    Widgets::Click();
    state.timeToggleRequested = true;
  }
  bool hovered = ImGui::IsItemHovered();

  ImVec2 c{pos.x + size * 0.5f, pos.y + size * 0.5f};
  float r = size * 0.5f - 3;
  float t = static_cast<float>(ImGui::GetTime());
  dl->AddCircleFilled(c, r + 2, hovered && clickable ? Rgba(45, 45, 62)
                                                     : Rgba(28, 28, 38));

  const char *tip = nullptr;
  switch (state.clock) {
  case ClockLook::Running: {
    ImU32 amber = Rgba(255, 190, 70);
    dl->AddCircle(c, r, amber, 40, 2.5f);
    // A small sweep so it reads as "time flowing"
    float sweep = std::fmod(t * 0.8f, 1.0f);
    Arc(dl, c, r - 5, sweep, sweep + 0.12f, Rgba(255, 190, 70, 150), 2.0f);
    Hourglass(dl, c, r * 0.38f, amber);
    tip = "Stop time  (Space)";
    break;
  }
  case ClockLook::Stopped: {
    ImU32 cyan = Rgba(110, 210, 255);
    dl->AddCircle(c, r, Rgba(60, 90, 110), 40, 3.0f);
    Arc(dl, c, r, 0.0f, std::clamp(state.clockProgress, 0.0f, 1.0f), cyan,
        3.5f);
    PlayIcon(dl, c, r * 0.36f, cyan);
    tip = "Time stopped: click cast spots, then let time flow  (Space)";
    break;
  }
  case ClockLook::Executing: {
    dl->AddCircle(c, r, Rgba(70, 70, 85), 40, 3.0f);
    Arc(dl, c, r, 0.0f, std::clamp(state.clockProgress, 0.0f, 1.0f),
        Rgba(230, 230, 240, 200), 3.5f);
    dl->AddCircleFilled(c, r * 0.18f, Rgba(230, 230, 240, 160));
    tip = "The turn is playing out";
    break;
  }
  case ClockLook::Waiting: {
    // Pulses to say "your move"
    int a = 150 + static_cast<int>(100 * (0.5f + 0.5f * std::sin(t * 4.0f)));
    ImU32 amber = Rgba(255, 190, 70, a);
    dl->AddCircle(c, r, amber, 40, 2.5f);
    dl->AddCircle(c, r + 3 + 2 * std::sin(t * 4.0f), Rgba(255, 190, 70, a / 3),
                  40, 1.5f);
    Hourglass(dl, c, r * 0.38f, amber);
    tip = "Stop time to plan your turn  (Space)";
    break;
  }
  case ClockLook::Over:
    dl->AddCircle(c, r, Rgba(60, 60, 70), 40, 2.0f);
    Hourglass(dl, c, r * 0.38f, Rgba(90, 90, 100));
    tip = "Round over";
    break;
  }
  if (hovered && tip)
    ImGui::SetTooltip("%s", tip);
}

void UI::DrawHotbar(UIState &state, ImVec2 pos, ImVec2 size) {
  const float PAD = Px(BASE_PAD);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float slotW = std::min(Px(170), (size.x - PAD * (DECK_SLOTS - 1)) / DECK_SLOTS);
  bool inMatch = state.matchRound >= 0;

  for (int i = 0; i < DECK_SLOTS; ++i) {
    ImVec2 p0{pos.x + i * (slotW + PAD), pos.y};
    ImVec2 p1{p0.x + slotW, p0.y + size.y};
    const Spell *spell = SlotSpell(state, i);
    bool selected = m_selectedSlot == i;

    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(i);
    if (Widgets::InvisibleButton("##slot", {slotW, size.y})) {
      SelectSlot(i);
      if (!m_gameMode)
        state.tool = SandboxTool::Cast;
    }
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    SpellStats stats =
        spell ? SpellQuant::Canonical(*spell) : SpellStats{};
    int ticks = spell && stats.valid ? TurnController::CastTicks(stats) : 0;
    // Greyed out when it won't fit in what's left of this turn
    bool fits = !inMatch || ticks <= state.ticksFree;
    bool usable = spell && stats.valid && fits;
    unsigned char alpha = usable ? 255 : 110;

    dl->AddRectFilled(p0, p1,
                      selected  ? Rgba(44, 52, 82)
                      : hovered ? Rgba(36, 36, 50)
                                : Rgba(24, 24, 33),
                      6.0f);
    dl->AddRect(p0, p1,
                selected ? Rgba(130, 170, 255) : Rgba(55, 55, 72), 6.0f, 0,
                selected ? 2.0f : 1.0f);

    float thumb = size.y - 6;
    ImVec2 t0{p0.x + 3, p0.y + 3};
    if (spell)
      m_thumbnails->Draw(dl, *spell, t0, {t0.x + thumb, t0.y + thumb}, alpha);

    // Key badge in the corner
    ImVec2 badge{p0.x + 9, p0.y + 9};
    dl->AddCircleFilled(badge, 7.5f, Rgba(10, 10, 16, 220));
    char key[2] = {static_cast<char>('1' + i), 0};
    dl->AddText({badge.x - 3.5f, badge.y - 7}, Rgba(235, 235, 245), key);

    float tx = t0.x + thumb + 5;
    float textW = p1.x - tx - 5;
    if (!spell) {
      dl->AddText({tx, p0.y + size.y * 0.5f - 7}, Rgba(110, 110, 130),
                  "empty");
      continue;
    }

    // Name, clipped to the slot
    ImGui::PushClipRect({tx, p0.y}, {p1.x - 4, p1.y}, true);
    Color tint = SpellThumbnails::Tint(*spell);
    dl->AddText({tx, p0.y + 6}, FromColor(tint, alpha), spell->name.c_str());
    ImGui::PopClipRect();

    // Cast time as a share of the 3 second turn
    ImVec2 b0{tx, p1.y - 14};
    ImVec2 b1{tx + textW, p1.y - 8};
    dl->AddRectFilled(b0, b1, Rgba(40, 40, 55), 2.0f);
    if (stats.valid) {
      float frac = std::min(1.0f, (float)ticks / TurnController::TURN_TICKS);
      dl->AddRectFilled(b0, {b0.x + textW * frac, b1.y},
                        fits ? Rgba(255, 190, 70, alpha)
                             : Rgba(200, 80, 60, 200),
                        2.0f);
      dl->AddText(ImGui::GetFont(), Px(12), {tx, p1.y - Px(28)},
                  Rgba(170, 170, 190, alpha),
                  TextFormat("%.2fs", ticks * TurnController::TICK_DT));
    } else {
      dl->AddText(ImGui::GetFont(), Px(12), {tx, p1.y - Px(28)},
                  Rgba(230, 90, 90), "invalid");
    }

    if (hovered) {
      ImGui::BeginTooltip();
      ImGui::Text("%s  [%d]", spell->name.c_str(), i + 1);
      ImGui::Separator();
      SpellEditor::DrawStats(stats);
      if (stats.valid)
        ImGui::Text("Cast time: %.2fs of %.0fs",
                    ticks * TurnController::TICK_DT,
                    TurnController::TURN_SECONDS);
      if (!fits)
        ImGui::TextColored({1, 0.5f, 0.4f, 1}, "Not enough time left this turn");
      if (i == 0 && !m_testSpellRef.empty() && !inMatch)
        ImGui::TextDisabled("Testing from the editor");
      ImGui::EndTooltip();
    }
  }
}

void UI::DrawMaterials(UIState &state, ImVec2 pos, ImVec2 size) {
  const float PAD = Px(BASE_PAD);
  struct Material {
    Element element;
    const char *label;
    Color col;
  };
  static constexpr Material kMaterials[] = {
      {Element::WATER, "Water", {64, 164, 223, 255}},
      {Element::EARTH, "Earth", {100, 60, 20, 255}},
      {Element::FIRE, "Fire", {220, 80, 0, 255}},
      {Element::STEAM, "Steam", {150, 150, 170, 255}},
      {Element::CLOUD, "Cloud", {170, 170, 200, 255}},
      {Element::ICE, "Ice", {110, 200, 230, 255}},
      {Element::SAND, "Sand", {200, 160, 80, 255}},
      {Element::ROCK, "Rock", {90, 90, 90, 255}},
      {Element::WOOD, "Wood", {120, 78, 40, 255}},
      {Element::GRASS, "Grass", {70, 150, 55, 255}},
      {Element::AIR, "Erase", {55, 55, 60, 255}},
  };
  constexpr int count = static_cast<int>(std::size(kMaterials));
  float brushW = 90.0f;
  float w = std::min(Px(84), (size.x - brushW - PAD * count) / count);
  ImDrawList *dl = ImGui::GetWindowDrawList();

  for (int i = 0; i < count; ++i) {
    const Material &m = kMaterials[i];
    ImVec2 p0{pos.x + i * (w + PAD), pos.y + 6};
    ImVec2 p1{p0.x + w, pos.y + size.y - 6};
    bool selected = state.selectedMaterial == m.element;
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushID(i);
    if (Widgets::InvisibleButton("##mat", {w, p1.y - p0.y}))
      state.selectedMaterial = m.element;
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    dl->AddRectFilled(p0, p1, FromColor(m.col, hovered ? 255 : 220), 5.0f);
    dl->AddRect(p0, p1, selected ? Rgba(255, 255, 255) : Rgba(20, 20, 26),
                5.0f, 0, selected ? 2.5f : 1.0f);
    ImVec2 ts = ImGui::CalcTextSize(m.label);
    dl->AddText({p0.x + (w - ts.x) * 0.5f, p0.y + (p1.y - p0.y - ts.y) * 0.5f},
                Rgba(250, 250, 250), m.label);
    if (hovered && i < 9)
      ImGui::SetTooltip("Key %d", i + 1);
  }

  float bx = pos.x + count * (w + PAD) + 4;
  dl->AddText({bx, pos.y + 8}, Rgba(200, 200, 215), "Brush");
  dl->AddText(ImGui::GetFont(), Px(22), {bx, pos.y + Px(24)}, Rgba(240, 240, 250),
              TextFormat("%d", state.brushRadius));
  dl->AddText(ImGui::GetFont(), Px(12), {bx + Px(30), pos.y + Px(32)},
              Rgba(140, 140, 160), "wheel");
}

void UI::DrawDeckPicker(UIState &state, ImVec2 pos, ImVec2 size) {
  const float PAD = Px(BASE_PAD);
  constexpr float playW = 58.0f;
  ImGui::SetCursorScreenPos(pos);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.32f, 0.12f, 1));
  if (Widgets::Button("Play", {playW, 0}))
    state.menuRequested = true;
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Practice match, online play, history and replays  (M)");
  ImGui::SameLine(0, PAD);
  ImGui::SetNextItemWidth(size.x - playW - PAD);
  bool inMatch = state.matchRound >= 0;
  const Deck *deck = HotbarDeck(state);
  std::string label = deck ? deck->name : "No deck";
  if (inMatch)
    label = TextFormat("Round %d: %s", state.matchRound + 1, label.c_str());

  ImGui::BeginDisabled(inMatch);
  if (ImGui::BeginCombo("##deck", label.c_str())) {
    for (const Deck &d : m_decks.Decks()) {
      bool active = d.id == m_decks.ActiveId();
      if (ImGui::Selectable(
              TextFormat("%s  (%d/%d)", d.name.c_str(), d.Filled(), DECK_SLOTS),
              active))
        m_decks.SetActive(d.id);
    }
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  if (inMatch && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Decks are locked for the match");

  float by = pos.y + size.y - ImGui::GetFrameHeight();
  float half = (size.x - PAD) * 0.5f;
  ImGui::SetCursorScreenPos({pos.x, by});
  if (Widgets::Button("Spells & decks", {m_gameMode ? size.x : half, 0}))
    m_spellEditor.OpenLibrary();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Spell editor, library and decks  (E)");
  if (!m_gameMode) {
    ImGui::SameLine(0, PAD);
    if (Widgets::Button("Reset avatar", {half, 0}))
      state.resetAvatarRequested = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Full health, back where you last placed it.\n"
                        "Drag the avatar or right-click to move it.");
  }
}
