#include "whas/engine/view.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_editor.h"
#include "whas/spell/spell_quant.h"
#include "whas/ui/spell_thumbnails.h"
#include "whas/ui/widgets.h"
#include "whas/spell/spell_rules.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// The editor's "Library & decks" tab: a filterable grid of saved spells on
// the left, decks and the per-round match decks on the right. Cards drag
// into deck slots.

using Theme::Tone;

namespace {

constexpr const char *SPELL_PAYLOAD = "WHAS_SPELL_REF";
// Sizes at 720p; the UI scale grows them
float Px(float v) { return v * View::UiScale(); }
float CardW() { return Px(118); }
float CardH() { return Px(150); }
float DeckPanelW() { return Px(430); }

struct Filter {
  const char *label;
  const char *sigil; // nullptr = all
};
constexpr Filter kFilters[] = {
    {"All", nullptr},     {"Fire", "fire"},   {"Water", "water"},
    {"Earth", "earth"},   {"Light", "light"}, {"Wind", "wind"},
    {"Underfoot", "wind_underfoot"},
};

// The sigil that picks what a plain spell (or a component) does
const char *SigilOf(const std::vector<PlacedGlyph> &glyphs) {
  for (const PlacedGlyph &g : glyphs)
    if (g.kind == GlyphKind::Sigil && !SpellSystem::IsShapeSigil(g.assetId))
      return g.assetId.c_str();
  return "";
}

// A layered spell matches the sigil of any of its parts
bool HasSigil(const Spell &spell, const char *sigil) {
  if (std::strcmp(SigilOf(spell.glyphs), sigil) == 0)
    return true;
  return std::any_of(spell.components.begin(), spell.components.end(),
                     [sigil](const SpellComponent &c) {
                       return std::strcmp(SigilOf(c.glyphs), sigil) == 0;
                     });
}

bool ContainsNoCase(const std::string &hay, const char *needle) {
  if (!needle[0])
    return true;
  auto lower = [](unsigned char c) { return std::tolower(c); };
  std::string h(hay), n(needle);
  std::transform(h.begin(), h.end(), h.begin(), lower);
  std::transform(n.begin(), n.end(), n.begin(), lower);
  return h.find(n) != std::string::npos;
}

ImU32 ToU32(Color c, unsigned char a = 255) {
  return IM_COL32(c.r, c.g, c.b, a);
}

// Marks a spell over the sign limit (it only plays solo or in chaos rooms)
void DrawChaosBadge(ImDrawList *dl, ImVec2 c) {
  Theme::Diamond(dl, c, Px(6), Theme::U32(Tone::Oxblood));
  Theme::Diamond(dl, c, Px(2.5f), Theme::U32(Tone::Ink));
}

} // namespace

void SpellEditor::DrawLibraryTab() {
  if (!m_spells || !m_decks)
    return;
  if (!m_decks->Find(m_selectedDeck))
    m_selectedDeck = m_decks->ActiveId();

  float gridW = ImGui::GetContentRegionAvail().x - DeckPanelW() - 8;
  ImGui::BeginChild("SpellGrid", {gridW, 0}, true);
  DrawSpellGrid(gridW);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("DeckPanel", {DeckPanelW(), 0}, true);
  DrawDeckPanel();
  ImGui::Separator();
  DrawRoundDecks();
  ImGui::EndChild();

  DrawLibraryPopups();
}

void SpellEditor::DrawSpellGrid(float width) {
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##search", "Search spells", m_search,
                           sizeof m_search);
  for (int i = 0; i < (int)std::size(kFilters); ++i) {
    ImGui::SameLine();
    bool on = m_filter == i;
    if (Widgets::SmallButton(kFilters[i].label, on))
      m_filter = i;
  }
  ImGui::TextDisabled(
      "Drag a card onto a deck slot. Right-click for more. Double-click to edit.");
  ImGui::Spacing();

  int columns = std::max(1, (int)((width - 16) / (CardW() + 8)));
  int shown = 0;
  for (const Spell &spell : m_spells->All()) {
    const char *sigil = kFilters[m_filter].sigil;
    if (sigil && !HasSigil(spell, sigil))
      continue;
    if (!ContainsNoCase(spell.name, m_search))
      continue;
    if (shown % columns != 0)
      ImGui::SameLine(0, 8);
    DrawSpellCard(spell, {CardW(), CardH()});
    ++shown;
  }
  if (shown == 0)
    ImGui::TextDisabled(m_spells->All().empty()
                            ? "No spells yet. Make one in the Edit tab."
                            : "No spells match.");
}

void SpellEditor::DrawSpellCard(const Spell &spell, ImVec2 size) {
  std::string ref = m_spells->RefOf(spell);
  ImGui::PushID(ref.c_str());
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1{p0.x + size.x, p0.y + size.y};
  ImGui::InvisibleButton("##card", size);
  bool hovered = ImGui::IsItemHovered();
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    Widgets::Click();
    OpenSpell(spell);
  }

  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(SPELL_PAYLOAD, ref.c_str(), ref.size() + 1);
    if (m_thumbnails) {
      ImVec2 at = ImGui::GetCursorScreenPos();
      m_thumbnails->Draw(ImGui::GetWindowDrawList(), spell, at,
                         {at.x + 48, at.y + 48});
      ImGui::Dummy({48, 48});
    }
    ImGui::Text("%s", spell.name.c_str());
    ImGui::EndDragDropSource();
  }

  if (ImGui::BeginPopupContextItem("##cardmenu")) {
    if (ImGui::MenuItem("Open / edit"))
      OpenSpell(spell);
    if (ImGui::MenuItem("Duplicate")) {
      std::string newRef, err;
      m_statusMessage = m_spells->Duplicate(ref, newRef, err)
                            ? "Duplicated " + spell.name
                            : err;
    }
    if (ImGui::MenuItem("Rename...")) {
      m_popupRef = ref;
      std::strncpy(m_renameBuffer, spell.name.c_str(), SPELL_NAME_MAX_LEN);
      m_renameBuffer[SPELL_NAME_MAX_LEN] = '\0';
      m_openPopup = "Rename spell";
    }
    if (ImGui::MenuItem("Delete...")) {
      m_popupRef = ref;
      m_openPopup = "Delete spell?";
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Test in sandbox"))
      m_testRef = ref;
    if (const Deck *deck = m_decks->Find(m_selectedDeck)) {
      auto empty = std::find(deck->slots.begin(), deck->slots.end(), "");
      if (ImGui::MenuItem(TextFormat("Add to \"%s\"", deck->name.c_str()),
                          nullptr, false, empty != deck->slots.end()))
        m_decks->SetSlot(deck->id, (int)(empty - deck->slots.begin()), ref);
    }
    ImGui::EndPopup();
  }

  SpellStats stats = SpellQuant::Canonical(spell);
  if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    ImGui::BeginTooltip();
    ImGui::Text("%s", spell.name.c_str());
    ImGui::Separator();
    DrawStats(stats);
    if (!SpellRules::WithinLimits(spell))
      ImGui::TextColored(Theme::Vec(Tone::Oxblood),
                         "Over the limit: solo and chaos rooms only");
    ImGui::EndTooltip();
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  Color tint = SpellThumbnails::Tint(spell);
  dl->AddRectFilled(p0, p1, hovered ? Theme::U32(Tone::UmberHi)
                                    : Theme::U32(Tone::Soot),
                    8.0f);
  dl->AddRect(p0, p1, ToU32(tint, hovered ? 220 : 90), 8.0f, 0, 1.5f);
  float thumb = size.x - 14;
  ImVec2 t0{p0.x + 7, p0.y + 7};
  if (m_thumbnails)
    m_thumbnails->Draw(dl, spell, t0, {t0.x + thumb, t0.y + thumb});

  ImGui::PushClipRect(p0, {p1.x - 4, p1.y}, true);
  dl->AddText({p0.x + 8, t0.y + thumb + 4}, Theme::U32(Tone::Parchment),
              spell.name.c_str());
  ImGui::PopClipRect();
  const char *kind = !stats.valid                         ? "invalid"
                     : stats.kind == SpellKind::Compound
                         ? TextFormat("layered x%d", (int)stats.parts.size())
                     : stats.kind == SpellKind::Flight    ? "underfoot"
                     : stats.kind == SpellKind::Field
                         ? (stats.pull > 0.0f ? "pull" : "push")
                                                          : SigilOf(spell.glyphs);
  int ticks = stats.valid ? TurnController::CastTicks(stats) : 0;
  dl->AddText(ImGui::GetFont(), Px(12), {p0.x + 8, t0.y + thumb + Px(20)},
              stats.valid ? ToU32(tint, 200) : Theme::U32(Tone::Oxblood),
              TextFormat("%s  %.2fs", kind, ticks * TurnController::TICK_DT));
  if (!SpellRules::WithinLimits(spell))
    DrawChaosBadge(dl, {p1.x - Px(8), p0.y + Px(8)});
  ImGui::PopID();
}

void SpellEditor::DrawDeckPanel() {
  ImGui::PushFont(Theme::Heading(), 0.0f);
  ImGui::TextColored(Theme::Vec(Tone::Brass), "Decks");
  ImGui::PopFont();
  ImGui::SameLine(DeckPanelW() - Px(250));
  if (Widgets::SmallButton("New")) {
    m_selectedDeck = m_decks->Create("New deck").id;
  }
  ImGui::SameLine();
  if (Widgets::SmallButton("Duplicate")) {
    std::string id = m_decks->Duplicate(m_selectedDeck);
    if (!id.empty())
      m_selectedDeck = id;
  }
  ImGui::SameLine();
  if (Widgets::SmallButton("Rename")) {
    if (const Deck *d = m_decks->Find(m_selectedDeck)) {
      m_popupDeck = d->id;
      std::strncpy(m_renameBuffer, d->name.c_str(), SPELL_NAME_MAX_LEN);
      m_renameBuffer[SPELL_NAME_MAX_LEN] = '\0';
      m_openPopup = "Rename deck";
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(m_decks->Decks().size() <= 1);
  if (Widgets::SmallButton("Delete")) {
    m_popupDeck = m_selectedDeck;
    m_openPopup = "Delete deck?";
  }
  ImGui::EndDisabled();

  ImGui::BeginChild("DeckList", {0, 96}, true);
  for (const Deck &d : m_decks->Decks()) {
    bool active = d.id == m_decks->ActiveId();
    if (ImGui::Selectable(
            TextFormat("%s%s  (%d/%d)", active ? "* " : "", d.name.c_str(),
                       d.Filled(), DECK_SLOTS),
            d.id == m_selectedDeck))
      m_selectedDeck = d.id;
  }
  ImGui::EndChild();

  if (const Deck *deck = m_decks->Find(m_selectedDeck)) {
    DrawDeckSlots(*deck);
    bool active = deck->id == m_decks->ActiveId();
    ImGui::BeginDisabled(active);
    if (Widgets::Button(active ? "In use in the sandbox" : "Use in the sandbox"))
      m_decks->SetActive(deck->id);
    ImGui::EndDisabled();
  }
}

void SpellEditor::DrawDeckSlots(const Deck &deck) {
  constexpr float slot = 64.0f;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < DECK_SLOTS; ++i) {
    if (i % 6 != 0)
      ImGui::SameLine(0, 5);
    ImGui::PushID(i);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1{p0.x + slot, p0.y + slot};
    ImGui::InvisibleButton("##deckslot", {slot, slot});
    bool hovered = ImGui::IsItemHovered();

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload(SPELL_PAYLOAD))
        m_decks->SetSlot(deck.id, i, static_cast<const char *>(payload->Data));
      ImGui::EndDragDropTarget();
    }
    const Spell *spell = m_spells->Find(deck.slots[i]);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      Widgets::Click();
      m_decks->SetSlot(deck.id, i, "");
    }
    // Slots can be reordered by dragging one onto another
    if (spell && ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(SPELL_PAYLOAD, deck.slots[i].c_str(),
                                deck.slots[i].size() + 1);
      ImGui::Text("%s", spell->name.c_str());
      ImGui::EndDragDropSource();
    }

    bool dropping = ImGui::GetDragDropPayload() &&
                    ImGui::GetDragDropPayload()->IsDataType(SPELL_PAYLOAD);
    dl->AddRectFilled(p0, p1, Theme::U32(Tone::Soot), 6.0f);
    dl->AddRect(p0, p1,
                dropping && hovered ? Theme::U32(Tone::BrassBright)
                : dropping          ? Theme::U32(Tone::Brass)
                                    : Theme::U32(Tone::BrassDim),
                6.0f, 0, dropping ? 2.0f : 1.0f);
    if (spell && m_thumbnails)
      m_thumbnails->Draw(dl, *spell, {p0.x + 3, p0.y + 3},
                         {p1.x - 3, p1.y - 3});
    else if (!deck.slots[i].empty())
      dl->AddText({p0.x + 6, p0.y + 24}, Theme::U32(Tone::Oxblood),
                  "missing");
    dl->AddText({p0.x + 4, p0.y + 2}, Theme::U32(Tone::Parchment),
                TextFormat("%d", i + 1));
    if (spell && !SpellRules::WithinLimits(*spell))
      DrawChaosBadge(dl, {p1.x - Px(7), p0.y + Px(7)});
    if (hovered && spell)
      ImGui::SetTooltip("%s%s\nRight-click to clear", spell->name.c_str(),
                        SpellRules::WithinLimits(*spell)
                            ? ""
                            : "\nOver the limit: solo and chaos rooms only");
    else if (hovered)
      ImGui::SetTooltip("Drop a spell here");
    ImGui::PopID();
  }
}

void SpellEditor::DrawRoundDecks() {
  Widgets::SectionHeader("Match decks");
  ImGui::TextDisabled("The deck you bring to each round of a best-of-3.");
  const MatchDecks &match = m_decks->Match();
  for (int r = 0; r < MATCH_ROUNDS; ++r) {
    ImGui::PushID(r);
    const Deck *deck = m_decks->Find(match.deckIds[r]);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Round %d", r + 1);
    ImGui::SameLine(80 * View::UiScale());
    ImGui::SetNextItemWidth(150 * View::UiScale());
    if (ImGui::BeginCombo("##round", deck ? deck->name.c_str() : "-")) {
      for (const Deck &d : m_decks->Decks())
        if (ImGui::Selectable(d.name.c_str(), deck && d.id == deck->id))
          m_decks->AssignRound(r, d.id);
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (Widgets::SmallButton("Copy from..."))
      ImGui::OpenPopup("copyfrom");
    if (ImGui::BeginPopup("copyfrom")) {
      for (int o = 0; o < MATCH_ROUNDS; ++o) {
        if (o == r)
          continue;
        const Deck *other = m_decks->Find(match.deckIds[o]);
        if (ImGui::MenuItem(TextFormat("Round %d (%s)", o + 1,
                                       other ? other->name.c_str() : "-")))
          m_decks->AssignRound(r, match.deckIds[o]);
      }
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (Widgets::SmallButton("Edit copy")) {
      // Branch this round off into its own deck and select it for editing
      std::string id = m_decks->Duplicate(match.deckIds[r]);
      if (!id.empty()) {
        m_decks->Rename(id, TextFormat("Round %d deck", r + 1));
        m_decks->AssignRound(r, id);
        m_selectedDeck = id;
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Make a copy of this deck just for round %d and "
                        "edit it above",
                        r + 1);

    // Mini preview of the six slots
    if (deck && m_thumbnails) {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      p.x += 70;
      for (int i = 0; i < DECK_SLOTS; ++i) {
        ImVec2 q0{p.x + i * 30.0f, p.y};
        dl->AddRectFilled(q0, {q0.x + 26, q0.y + 26},
                          Theme::U32(Tone::Soot), 4.0f);
        if (const Spell *s = m_spells->Find(deck->slots[i]))
          m_thumbnails->Draw(dl, *s, {q0.x + 1, q0.y + 1},
                             {q0.x + 25, q0.y + 25});
      }
      ImGui::Dummy({0, 30});
    }
    ImGui::PopID();
  }
}

void SpellEditor::DrawLibraryPopups() {
  if (m_openPopup) {
    ImGui::OpenPopup(m_openPopup);
    m_openPopup = nullptr;
  }
  auto close = [] { ImGui::CloseCurrentPopup(); };

  if (ImGui::BeginPopupModal("Rename spell", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetNextItemWidth(260);
    bool enter = ImGui::InputText("##name", m_renameBuffer,
                                  SPELL_NAME_MAX_LEN + 1,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    if (Widgets::Button("Rename") || enter) {
      std::string newRef, err;
      if (m_spells->Rename(m_popupRef, m_renameBuffer, newRef, err)) {
        m_decks->ReplaceRef(m_popupRef, newRef);
        m_statusMessage = "Renamed.";
        close();
      } else {
        m_statusMessage = err;
      }
    }
    ImGui::SameLine();
    if (Widgets::Button("Cancel"))
      close();
    if (!m_statusMessage.empty())
      ImGui::TextDisabled("%s", m_statusMessage.c_str());
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Delete spell?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const Spell *spell = m_spells->Find(m_popupRef);
    ImGui::Text("Delete \"%s\"? It is also removed from every deck.",
                spell ? spell->name.c_str() : m_popupRef.c_str());
    if (Widgets::Button("Delete")) {
      std::string err;
      if (m_spells->Remove(m_popupRef, err))
        m_decks->RemoveRef(m_popupRef);
      m_statusMessage = err.empty() ? "Deleted." : err;
      close();
    }
    ImGui::SameLine();
    if (Widgets::Button("Cancel"))
      close();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Rename deck", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetNextItemWidth(260);
    bool enter = ImGui::InputText("##deckname", m_renameBuffer,
                                  SPELL_NAME_MAX_LEN + 1,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    if ((Widgets::Button("Rename") || enter) && m_renameBuffer[0]) {
      m_decks->Rename(m_popupDeck, m_renameBuffer);
      close();
    }
    ImGui::SameLine();
    if (Widgets::Button("Cancel"))
      close();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Delete deck?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const Deck *deck = m_decks->Find(m_popupDeck);
    ImGui::Text("Delete the deck \"%s\"? Its spells are kept.",
                deck ? deck->name.c_str() : "?");
    if (Widgets::Button("Delete")) {
      m_decks->Remove(m_popupDeck);
      m_selectedDeck = m_decks->ActiveId();
      close();
    }
    ImGui::SameLine();
    if (Widgets::Button("Cancel"))
      close();
    ImGui::EndPopup();
  }
}

// Under the canvas: the spell as it will look in the hotbar
void SpellEditor::DrawPreviewStrip(ImVec2 size) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 p1{p0.x + std::min(size.x, 320.0f), p0.y + size.y};
  dl->AddRectFilled(p0, p1, Theme::U32(Tone::Soot), 6.0f);
  dl->AddRect(p0, p1, Theme::U32(Tone::BrassBright), 6.0f, 0, 2.0f);

  float thumb = size.y - 8;
  if (m_thumbnails)
    m_thumbnails->Draw(dl, m_currentSpell, {p0.x + 4, p0.y + 4},
                       {p0.x + 4 + thumb, p0.y + 4 + thumb});
  float tx = p0.x + thumb + 12;
  Color tint = SpellThumbnails::Tint(m_currentSpell);
  dl->AddText({tx, p0.y + 8}, ToU32(tint),
              m_nameBuffer[0] ? m_nameBuffer : "(unnamed)");

  SpellStats stats = SpellQuant::Canonical(m_currentSpell);
  float barW = p1.x - tx - 10;
  ImVec2 b0{tx, p1.y - 16};
  dl->AddRectFilled(b0, {b0.x + barW, b0.y + 6}, Theme::U32(Tone::Ink),
                    2.0f);
  if (stats.valid) {
    int ticks = TurnController::CastTicks(stats);
    float frac = std::min(1.0f, (float)ticks / TurnController::TURN_TICKS);
    dl->AddRectFilled(b0, {b0.x + barW * frac, b0.y + 6},
                      Theme::U32(Tone::Brass), 2.0f);
    bool tooSlow = ticks > TurnController::TURN_TICKS;
    dl->AddText(ImGui::GetFont(), Px(12), {tx, p1.y - Px(32)},
                tooSlow ? Theme::U32(Tone::Oxblood)
                        : Theme::U32(Tone::Muted),
                TextFormat(tooSlow ? "cast time %.2fs: longer than a %.0fs turn"
                                   : "cast time %.2fs of %.0fs",
                           ticks * TurnController::TICK_DT,
                           TurnController::TURN_SECONDS));
  } else {
    // The problem text, clipped to the strip (the stats panel has it whole)
    std::string problem = SpellSystem::Problem(m_currentSpell);
    ImGui::PushClipRect(p0, p1, true);
    dl->AddText(ImGui::GetFont(), Px(12), {tx, p1.y - Px(32)},
                Theme::U32(Tone::Oxblood),
                problem.empty() ? "not a working spell yet" : problem.c_str());
    ImGui::PopClipRect();
  }
  ImGui::Dummy(size);
}
