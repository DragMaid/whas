#include "whas/ui/spell_picker.h"
#include "imgui_stdlib.h"
#include "whas/engine/view.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/glyph_docs.h"
#include "whas/spell/spell_editor.h"
#include "whas/spell/spell_library.h"
#include "whas/spell/spell_quant.h"
#include "whas/ui/spell_thumbnails.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <cctype>
#include <string>

using Theme::Tone;

namespace SpellPicker {

namespace {

bool HasSpell(const std::vector<Spell> &spells, const std::string &name) {
  return std::any_of(spells.begin(), spells.end(),
                     [&](const Spell &s) { return s.name == name; });
}

bool Matches(const std::string &name, const std::string &search) {
  auto lower = [](std::string s) {
    for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  return search.empty() || lower(name).find(lower(search)) != std::string::npos;
}

} // namespace

void Card(const Spell &spell, ImVec2 size, SpellThumbnails &thumbs, bool selected,
          bool dim) {
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1{p0.x + size.x, p0.y + size.y};
  ImGui::InvisibleButton("##card", size);
  bool hovered = ImGui::IsItemHovered();
  if (ImGui::BeginDragDropSource()) {
    const Spell *ptr = &spell;
    ImGui::SetDragDropPayload(PAYLOAD, &ptr, sizeof ptr);
    ImVec2 at = ImGui::GetCursorScreenPos();
    thumbs.Draw(ImGui::GetWindowDrawList(), spell, at, {at.x + 48, at.y + 48});
    ImGui::Dummy({48, 48});
    ImGui::Text("%s", spell.name.c_str());
    ImGui::EndDragDropSource();
  }
  if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    Tooltip(spell);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  Color tint = SpellThumbnails::Tint(spell);
  float a = dim ? 0.45f : 1.0f;
  dl->AddRectFilled(p0, p1, Theme::U32(hovered ? Tone::UmberHi : Tone::Soot), 6.0f);
  dl->AddRect(p0, p1,
              selected ? Theme::U32(Tone::BrassBright)
                       : IM_COL32(tint.r, tint.g, tint.b, hovered ? 220 : 90),
              6.0f, 0, selected ? 2.5f : 1.5f);
  float label = ImGui::GetTextLineHeight();
  float pad = 4.0f;
  float side = std::min(size.x - 2 * pad, size.y - label - 2 * pad);
  ImVec2 t0{p0.x + (size.x - side) * 0.5f, p0.y + pad};
  thumbs.Draw(dl, spell, t0, {t0.x + side, t0.y + side},
              static_cast<unsigned char>(255 * a));
  // The name, cut to the card
  dl->PushClipRect({p0.x + pad, p0.y}, {p1.x - pad, p1.y}, true);
  ImVec2 ts = ImGui::CalcTextSize(spell.name.c_str());
  float x = ts.x < size.x - 2 * pad ? p0.x + (size.x - ts.x) * 0.5f : p0.x + pad;
  dl->AddText({x, p1.y - label - 2}, Theme::U32(Tone::Parchment, a), spell.name.c_str());
  dl->PopClipRect();
}

void Tooltip(const Spell &spell) {
  ImGui::BeginTooltip();
  ImGui::Text("%s", spell.name.c_str());
  ImGui::Separator();
  std::string glyphs;
  auto add = [&](const std::vector<PlacedGlyph> &list) {
    for (const PlacedGlyph &g : list) {
      GlyphDocs::Info info = GlyphDocs::Get(g.assetId);
      if (!glyphs.empty())
        glyphs += ", ";
      glyphs += info.name ? info.name : g.assetId;
      if (g.inverted)
        glyphs += " (inverted)";
    }
  };
  add(spell.glyphs);
  for (const SpellComponent &c : spell.components)
    add(c.glyphs);
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
  ImGui::TextDisabled("Glyphs: %s", glyphs.c_str());
  ImGui::PopTextWrapPos();
  SpellStats stats = SpellQuant::Canonical(spell);
  if (stats.valid)
    ImGui::Text("Cast time %.1fs",
                TurnController::CastTicks(stats) /
                    static_cast<float>(TurnController::TICKS_PER_SECOND));
  SpellEditor::DrawStats(stats);
  ImGui::EndTooltip();
}

const Spell *AcceptDrop() {
  const Spell *dropped = nullptr;
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(PAYLOAD))
      dropped = *static_cast<const Spell *const *>(p->Data);
    ImGui::EndDragDropTarget();
  }
  return dropped;
}

bool Draw(const char *id, std::vector<Spell> &chosen, const SpellLibrary &library,
          SpellThumbnails &thumbs) {
  float s = View::UiScale();
  bool changed = false;
  ImGui::PushID(id);
  ImVec2 card{64 * s, 64 * s + ImGui::GetTextLineHeight()};
  float gap = ImGui::GetStyle().ItemSpacing.x;

  // What's chosen; drop library cards here
  float width = ImGui::GetContentRegionAvail().x;
  int columns = std::max(1, static_cast<int>((width - 12 * s) / (card.x + gap)));
  int rows = std::max(1, (static_cast<int>(chosen.size()) + columns - 1) / columns);
  float height = rows * (card.y + ImGui::GetStyle().ItemSpacing.y) +
                 2 * ImGui::GetStyle().WindowPadding.y;
  ImGui::BeginChild("chosen", {0, height}, ImGuiChildFlags_Borders);
  int remove = -1;
  for (int i = 0; i < static_cast<int>(chosen.size()); ++i) {
    if (i % columns)
      ImGui::SameLine();
    ImGui::PushID(i);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    Card(chosen[i], card, thumbs);
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // An x in the corner takes it out
    float r = 8 * s;
    ImVec2 c{p0.x + card.x - r - 2, p0.y + r + 2};
    ImVec2 mouse = ImGui::GetMousePos();
    bool overX = hovered && (mouse.x - c.x) * (mouse.x - c.x) +
                                    (mouse.y - c.y) * (mouse.y - c.y) <= r * r;
    if (hovered) {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      dl->AddCircleFilled(c, r, Theme::U32(overX ? Tone::Oxblood : Tone::Ink, 0.9f));
      float k = r * 0.45f;
      dl->AddLine({c.x - k, c.y - k}, {c.x + k, c.y + k}, Theme::U32(Tone::Parchment), 1.5f);
      dl->AddLine({c.x - k, c.y + k}, {c.x + k, c.y - k}, Theme::U32(Tone::Parchment), 1.5f);
      if (overX && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        remove = i;
    }
    ImGui::PopID();
  }
  if (chosen.empty())
    ImGui::TextDisabled("None yet: drag spells up here, or double-click one.");
  ImGui::EndChild();
  if (const Spell *dropped = AcceptDrop(); dropped && !HasSpell(chosen, dropped->name)) {
    chosen.push_back(*dropped);
    changed = true;
  }
  if (remove >= 0) {
    chosen.erase(chosen.begin() + remove);
    changed = true;
  }

  // The library
  static std::string search;
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", "Search your spells", &search);
  ImGui::BeginChild("library", {0, 3.2f * (card.y + ImGui::GetStyle().ItemSpacing.y)},
                    ImGuiChildFlags_Borders);
  int shown = 0;
  for (const Spell &spell : library.All()) {
    if (!Matches(spell.name, search))
      continue;
    if (shown++ % columns)
      ImGui::SameLine();
    ImGui::PushID(spell.name.c_str());
    bool has = HasSpell(chosen, spell.name);
    Card(spell, card, thumbs, false, has);
    if (!has && ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      chosen.push_back(spell);
      changed = true;
    }
    ImGui::PopID();
  }
  if (shown == 0)
    ImGui::TextDisabled(library.All().empty() ? "Your library is empty."
                                              : "No spells match.");
  ImGui::EndChild();
  ImGui::PopID();
  return changed;
}

} // namespace SpellPicker
