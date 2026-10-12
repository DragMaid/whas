#include "imgui.h"
#include "whas/campaign/campaign_play.h"
#include "whas/engine/view.h"
#include "whas/spell/glyph_docs.h"
#include "whas/ui/spell_picker.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include "whas/ui/widgets.h"
#include <algorithm>

// The codex (J): every glyph (the known ones explained, with the
// campaign's lore), the blueprints in the backpack, and the people met.

using namespace Campaign;
using Theme::Tone;

namespace {

// A glyph's lines in a box
void DrawGlyph(ImDrawList *dl, const SvgAsset &a, ImVec2 min, float size, ImU32 color) {
  float k = size / std::max(a.viewWidth, a.viewHeight);
  ImVec2 o{min.x + (size - a.viewWidth * k) * 0.5f, min.y + (size - a.viewHeight * k) * 0.5f};
  for (const LineSeg &s : a.segments)
    dl->AddLine({o.x + s.a.x * k, o.y + s.a.y * k}, {o.x + s.b.x * k, o.y + s.b.y * k},
                color, 1.5f);
}

} // namespace

void CampaignPlay::DrawCodex(UI &ui) {
  float s = View::UiScale();
  ImGui::SetNextWindowSize({640 * s, 520 * s}, ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(
      {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
      ImGuiCond_Appearing, {0.5f, 0.5f});
  if (!ImGui::Begin("Codex", &m_codex, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("codex")) {
    if (ImGui::BeginTabItem("Glyphs")) {
      int known = 0, total = 0;
      for (GlyphKind kind : {GlyphKind::Sigil, GlyphKind::Sign})
        for (const SvgAsset *a : ui.Glyphs().GetByKind(kind)) {
          ++total;
          known += m_save.glyphs.count(a->id) > 0;
        }
      ImGui::TextDisabled("%d of %d known. Draw the ones you know at a workbench.", known,
                          total);
      ImGui::BeginChild("glyphs");
      float icon = 44 * s;
      for (GlyphKind kind : {GlyphKind::Sigil, GlyphKind::Sign}) {
        Widgets::SectionHeader(kind == GlyphKind::Sigil ? "Sigils" : "Signs");
        for (const SvgAsset *a : ui.Glyphs().GetByKind(kind)) {
          bool has = m_save.glyphs.count(a->id) > 0;
          ImVec2 p = ImGui::GetCursorScreenPos();
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(p, {p.x + icon, p.y + icon}, Theme::U32(Tone::Ink, 0.8f), 4.0f);
          if (has)
            DrawGlyph(dl, *a, {p.x + 4, p.y + 4}, icon - 8, Theme::U32(Tone::BrassBright));
          ImGui::Dummy({icon, icon});
          ImGui::SameLine();
          ImGui::BeginGroup();
          if (has) {
            GlyphDocs::Info info = GlyphDocs::Get(a->id);
            ImGui::TextColored(Theme::Vec(Tone::Brass), "%s",
                               info.name ? info.name : a->id.c_str());
            ImGui::PushTextWrapPos(0.0f);
            if (info.text && *info.text)
              ImGui::TextUnformatted(info.text);
            if (auto lore = m_def.lore.find(a->id);
                lore != m_def.lore.end() && !lore->second.empty())
              ImGui::TextColored(Theme::Vec(Tone::Faint), "%s", lore->second.c_str());
            ImGui::PopTextWrapPos();
          } else {
            ImGui::TextDisabled("???");
            ImGui::TextDisabled("Not yet learned.");
          }
          ImGui::EndGroup();
          ImGui::Spacing();
        }
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Blueprints")) {
      ImGui::TextDisabled("Everything in your backpack. Hover for what it is.");
      ImVec2 card{92 * s, 92 * s + ImGui::GetTextLineHeight()};
      float gap = ImGui::GetStyle().ItemSpacing.x;
      int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (card.x + gap)));
      int n = 0;
      for (const Spell &spell : m_backpackSpells.All()) {
        if (n++ % columns)
          ImGui::SameLine();
        ImGui::PushID(spell.name.c_str());
        SpellPicker::Card(spell, card, ui.Thumbnails(), false, false, false);
        ImGui::PopID();
      }
      if (m_backpackSpells.All().empty())
        ImGui::TextDisabled("Nothing yet.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("People")) {
      if (m_save.met.empty())
        ImGui::TextDisabled("You haven't spoken to anyone yet.");
      for (const auto &[key, name] : m_save.met) {
        int x = 0, y = 0;
        std::sscanf(key.c_str(), "%d_%d", &x, &y);
        ImGui::TextColored(Theme::Vec(Tone::Brass), "%s", name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("met in room (%d, %d)", x, y);
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
