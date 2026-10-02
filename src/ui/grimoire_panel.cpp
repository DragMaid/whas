#include "whas/ui/grimoire_panel.h"
#include "imgui.h"
#include "whas/engine/view.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include "whas/ui/widgets.h"
#include <algorithm>

using Theme::Tone;

void GrimoirePanel::Draw(const std::array<std::vector<RoundCards>, 2> &cards,
                         int localSlot) {
  if (cards[0].empty() && cards[1].empty())
    return;
  int opponent = localSlot >= 0 ? 1 - localSlot : 1;
  if (m_shown < 0)
    m_shown = opponent;

  float scale = View::UiScale();
  ImGui::SetNextWindowPos({GetScreenWidth() - 16.0f * scale, 16.0f * scale},
                          ImGuiCond_Appearing, {1.0f, 0.0f});
  if (!ImGui::Begin("Grimoire", nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  auto name = [&](int slot) -> const char * {
    if (localSlot < 0)
      return slot == 0 ? "Player 1" : "Player 2";
    return slot == localSlot ? "Yours" : "Opponent's";
  };
  for (int slot : {opponent, 1 - opponent}) {
    if (Widgets::Button(name(slot), {0, 0}, m_shown == slot))
      m_shown = slot;
    ImGui::SameLine();
  }
  ImGui::NewLine();
  ImGui::TextDisabled("Copy a spell, or a round's whole deck, into your library.");

  const std::vector<RoundCards> &rounds = cards[m_shown];
  for (int r = 0; r < static_cast<int>(rounds.size()); ++r)
    DrawRound(rounds[r], m_shown, r);
  if (!m_status.empty())
    ImGui::TextColored(Theme::Vec(Tone::Brass), "%s", m_status.c_str());
  ImGui::End();
}

void GrimoirePanel::DrawRound(const RoundCards &cards, int slot, int round) {
  ImGui::PushID(slot * 16 + round);
  char title[32];
  std::snprintf(title, sizeof title, "Round %d", round + 1);
  Widgets::SectionHeader(title);

  float scale = View::UiScale();
  float cell = 66.0f * scale;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  SpellLibrary &library = m_ui.Library();
  for (int i = 0; i < DECK_SLOTS; ++i) {
    if (i > 0)
      ImGui::SameLine();
    ImGui::PushID(i);
    ImGui::BeginGroup();
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b{a.x + cell, a.y + cell};
    const auto &card = cards[i];
    bool owned = card && library.FindSameDrawing(card->spell);
    ImGui::InvisibleButton("##card", {cell, cell});
    bool hovered = ImGui::IsItemHovered();
    Theme::Plate(dl, a, b, hovered, false, owned);
    if (card) {
      m_ui.Thumbnails().Draw(dl, card->spell, {a.x + 4, a.y + 4},
                             {b.x - 4, b.y - 4});
      if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(card->spell.name.c_str());
        if (owned)
          ImGui::TextDisabled("Already in your library");
        ImGui::EndTooltip();
      }
      ImGui::BeginDisabled(owned);
      if (Widgets::SmallButton(owned ? "Have it" : "Copy")) {
        std::string ref;
        if (CopySpell(card->spell, ref))
          m_status = "Copied \"" + card->spell.name + "\"";
      }
      ImGui::EndDisabled();
    } else {
      ImGui::TextDisabled("empty");
    }
    ImGui::EndGroup();
    ImGui::PopID();
  }
  if (Widgets::Button("Copy this deck", {cell * DECK_SLOTS + ImGui::GetStyle().ItemSpacing.x * (DECK_SLOTS - 1), 0}))
    CopyDeck(cards, round);
  ImGui::PopID();
}

bool GrimoirePanel::CopySpell(const Spell &spell, std::string &ref) {
  std::string error;
  if (!m_ui.Library().Import(spell, ref, error)) {
    m_status = error;
    return false;
  }
  return true;
}

void GrimoirePanel::CopyDeck(const RoundCards &cards, int round) {
  std::array<std::string, DECK_SLOTS> refs;
  for (int i = 0; i < DECK_SLOTS; ++i)
    if (cards[i] && !CopySpell(cards[i]->spell, refs[i]))
      return;
  char name[48];
  std::snprintf(name, sizeof name, "Copied round %d deck", round + 1);
  DeckBook &decks = m_ui.Decks();
  std::string id = decks.Create(name).id;
  for (int i = 0; i < DECK_SLOTS; ++i)
    if (!refs[i].empty())
      decks.SetSlot(id, i, refs[i]);
  m_status = std::string("Saved the deck \"") + name + "\"";
}
