#include "whas/engine/view.h"
#include "whas/ui/play_menu.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/ui/ui.h"
#include "whas/ui/audio_settings.h"
#include "whas/ui/theme.h"
#include "whas/ui/widgets.h"
#include <cfloat>
#include <chrono>
#include <cstring>

using Theme::Tone;

namespace {

template <typename T> bool Ready(const std::future<T> &f) {
  return f.valid() &&
         f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

const char *PhaseName(LockstepClient::Phase p) {
  using P = LockstepClient::Phase;
  switch (p) {
  case P::Offline:
    return "offline";
  case P::Connecting:
    return "connecting...";
  case P::Ready:
    return "connected";
  case P::Syncing:
    return "uploading spells and decks...";
  case P::Queued:
    return "looking for an opponent...";
  case P::Hosting:
    return "waiting for your friend...";
  case P::MatchOver:
    return "match over";
  default:
    return "in a match";
  }
}

} // namespace

std::string PlayMenu::ApiBase() const {
  return NetClient::HttpBase(m_client.Url().empty() ? m_url : m_client.Url()) +
         "/api";
}

void PlayMenu::SyncLibrary() {
  LockstepClient::Library lib;
  SpellLibrary &spells = m_ui.Library();
  for (const Spell &s : spells.All())
    lib.spells.push_back({spells.RefOf(s), s});
  lib.decks = m_ui.Decks().Decks();
  lib.match = m_ui.Decks().Match();
  m_client.SetLibrary(std::move(lib));
}

void PlayMenu::Draw() {
  if (Ready(m_historyRequest)) {
    m_history = m_historyRequest.get();
    if (!m_history)
      m_status = "Couldn't load match history";
  }
  if (Ready(m_replayRequest)) {
    m_replay = m_replayRequest.get();
    if (!m_replay)
      m_status = "Couldn't load that replay";
    else
      m_open = false;
  }
  // Server replies and errors ("that's your own lobby"...) while not in a
  // match; in a match the game shows them
  if (!m_client.InMatch())
    for (std::string &notice : m_client.TakeNotices())
      m_status = std::move(notice);
  if (!m_open)
    return;
  if (IsKeyPressed(KEY_ESCAPE) && !ImGui::GetIO().WantTextInput) {
    m_open = false;
    return;
  }

  float scale = View::UiScale();
  float sideW = 250.0f * scale;
  float h = static_cast<float>(GetScreenHeight());

  // Lamp-light falling off across the world behind the menu
  ImDrawList *bg = ImGui::GetBackgroundDrawList();
  float shade = sideW + 620.0f * scale;
  bg->AddRectFilledMultiColor({0, 0}, {shade, h}, Theme::U32(Tone::Ink, 0.85f),
                              Theme::U32(Tone::Ink, 0.0f), Theme::U32(Tone::Ink, 0.0f),
                              Theme::U32(Tone::Ink, 0.85f));

  DrawSidebar(sideW, h);
  if (m_page != Page::None)
    DrawPage({sideW + 18.0f * scale, 28.0f * scale}, 540.0f * scale);
}

void PlayMenu::DrawSidebar(float width, float height) {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({width, height});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Vec(Tone::Ink, 0.96f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::Begin("##sidebar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float scale = View::UiScale();

  // Brass edge with a darker inner line
  dl->AddLine({width - 1, 0}, {width - 1, height}, Theme::U32(Tone::BrassDim), 1.0f);
  dl->AddLine({width - 4, 0}, {width - 4, height}, Theme::U32(Tone::Line, 0.6f), 1.0f);

  // Crest: a slowly turning seal over the title
  float cx = width * 0.5f;
  float sealR = 46.0f * scale;
  float t = static_cast<float>(ImGui::GetTime());
  ImVec2 sealC{cx, 30.0f * scale + sealR};
  Theme::Seal(dl, sealC, sealR, Theme::U32(Tone::BrassDim), t * 0.05f);
  Theme::Seal(dl, sealC, sealR * 0.42f, Theme::U32(Tone::Brass, 0.8f), -t * 0.12f);
  float titleSize = 30.0f * scale;
  const char *title = "WITCH HAT";
  ImVec2 ts = Theme::Heading()->CalcTextSizeA(titleSize, FLT_MAX, 0, title);
  float y = sealC.y + sealR + 16.0f * scale;
  dl->AddText(Theme::Heading(), titleSize, {cx - ts.x * 0.5f, y + 1},
              Theme::U32(Tone::Ink), title);
  dl->AddText(Theme::Heading(), titleSize, {cx - ts.x * 0.5f, y},
              Theme::U32(Tone::Parchment), title);
  y += ts.y;
  float subSize = 15.0f * scale;
  const char *sub = "A T E L I E R";
  ImVec2 ss = Theme::Heading()->CalcTextSizeA(subSize, FLT_MAX, 0, sub);
  dl->AddText(Theme::Heading(), subSize, {cx - ss.x * 0.5f, y},
              Theme::U32(Tone::Brass), sub);
  y += ss.y + 14.0f * scale;
  Theme::Rule(dl, {24.0f * scale, y}, width - 48.0f * scale, Theme::U32(Tone::BrassDim));
  y += 18.0f * scale;

  ImGui::SetCursorScreenPos({0, y});
  auto page = [&](const char *label, Page p) {
    bool selected = m_page == p;
    if (Widgets::NavItem(label, selected, width - 6))
      m_page = selected ? Page::None : p;
  };
  auto action = [&](const char *label, auto &&run) {
    if (Widgets::NavItem(label, false, width - 6))
      run();
  };
  action("Sandbox", [&] {
    m_sandbox = true;
    m_open = false;
  });
  page("Solo Duel", Page::Solo);
  page("Online Duel", Page::Online);
  action("Spells & Decks", [&] {
    m_ui.OpenSpellLibrary();
    m_open = false;
  });
  action("Maps", [&] {
    m_maps.Open();
    m_open = false;
  });
  page("Replays", Page::Replays);
  page("Settings", Page::Settings);

  // Footer: connection and the latest word from the server
  float footY = height - 70.0f * scale;
  Theme::Rule(dl, {24.0f * scale, footY}, width - 48.0f * scale, Theme::U32(Tone::Line));
  ImGui::SetCursorScreenPos({0, footY + 10.0f * scale});
  ImGui::Indent(24.0f * scale);
  ImGui::PushTextWrapPos(width - 20.0f * scale);
  bool online = m_client.GetPhase() != LockstepClient::Phase::Offline;
  ImGui::TextColored(Theme::Vec(online ? Tone::Verdigris : Tone::Faint), "%s",
                     online ? PhaseName(m_client.GetPhase()) : "Not connected");
  if (!m_status.empty())
    ImGui::TextColored(Theme::Vec(Tone::Brass), "%s", m_status.c_str());
  else
    ImGui::TextDisabled("M or Esc closes this");
  ImGui::PopTextWrapPos();
  ImGui::Unindent(24.0f * scale);

  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void PlayMenu::DrawPage(ImVec2 pos, float width) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSizeConstraints({width, 0},
                                      {width, GetScreenHeight() - pos.y * 2});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      {22.0f * View::UiScale(), 18.0f * View::UiScale()});
  ImGui::Begin("##page", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar();
  ImVec2 a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  Theme::Plate(ImGui::GetWindowDrawList(), a, {a.x + size.x, a.y + size.y},
               false, false);
  ImGui::GetWindowDrawList()->AddRectFilled(
      {a.x + 4, a.y + 4}, {a.x + size.x - 4, a.y + size.y - 4},
      Theme::U32(Tone::Soot, 0.92f), 1.0f);

  switch (m_page) {
  case Page::Solo:
    Widgets::Title("Solo Duel");
    DrawSoloSetup();
    break;
  case Page::Online:
    Widgets::Title("Online Duel");
    DrawOnline();
    break;
  case Page::Replays:
    Widgets::Title("Replays");
    DrawHistory();
    break;
  case Page::Settings:
    Widgets::Title("Settings");
    Widgets::SectionHeader("Sound");
    DrawAudioSettings();
    break;
  case Page::None:
    break;
  }
  ImGui::End();
}

void PlayMenu::DrawOnline() {
  using P = LockstepClient::Phase;
  P phase = m_client.GetPhase();
  bool offline = phase == P::Offline;

  Widgets::SectionHeader("Server");
  ImGui::BeginDisabled(!offline);
  ImGui::SetNextItemWidth(-130.0f * View::UiScale());
  ImGui::InputText("##server", m_url, sizeof m_url);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (offline) {
    if (Widgets::Button("Connect", {-1, 0}))
      m_client.Connect(m_url);
  } else if (Widgets::Button("Disconnect", {-1, 0})) {
    m_client.Disconnect();
  }
  ImGui::TextDisabled("%s", PhaseName(phase));
  if (m_client.PlayerId() != 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("  - you are guest #%lld", (long long)m_client.PlayerId());
  }

  if (phase == P::Queued) {
    if (Widgets::Button("Stop looking"))
      m_client.CancelWaiting();
    return;
  }
  if (phase == P::Hosting) {
    Widgets::SectionHeader("Your room");
    ImGui::TextUnformatted("Give your friend this code:");
    ImGui::PushFont(Theme::Heading(), ImGui::GetFontSize() * 1.6f);
    ImGui::TextColored(Theme::Vec(Tone::BrassBright), "%s", m_client.LobbyCode().c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (Widgets::SmallButton("Copy"))
      ImGui::SetClipboardText(m_client.LobbyCode().c_str());
    if (Widgets::Button("Close the room"))
      m_client.CancelWaiting();
    return;
  }
  if (phase != P::Ready && phase != P::MatchOver)
    return;

  Widgets::SectionHeader("Your decks");
  const MatchDecks &rounds = m_ui.Decks().Match();
  ImGui::TextDisabled("Rounds 1-3 use:");
  for (int r = 0; r < MATCH_ROUNDS; ++r) {
    const Deck *d = m_ui.Decks().Find(rounds.deckIds[r]);
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", d ? d->name.c_str() : "?",
                        r + 1 < MATCH_ROUNDS ? "," : "");
  }
  ImGui::SameLine();
  if (Widgets::SmallButton("change"))
    m_ui.OpenSpellLibrary();

  if (auto running = m_client.RunningMatch()) {
    ImGui::TextColored(Theme::Vec(Tone::BrassBright),
                       "You have a match in progress (#%lld)", (long long)*running);
    if (Widgets::Button("Rejoin match", {200, 0}))
      m_client.Rejoin();
    return;
  }

  Widgets::SectionHeader("Find a duel");
  float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (Widgets::Button("Quick match", {half, 0})) {
    SyncLibrary();
    m_client.QuickMatch();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Anyone on the same game build; random arenas");
  ImGui::SameLine();
  if (Widgets::Button("Create a room", {half, 0}, m_roomSetup))
    m_roomSetup = !m_roomSetup;
  if (m_roomSetup)
    DrawRoomSetup();
  ImGui::SetNextItemWidth(half);
  ImGui::InputTextWithHint("##code", "ROOM CODE", m_code, sizeof m_code,
                           ImGuiInputTextFlags_CharsUppercase);
  ImGui::SameLine();
  ImGui::BeginDisabled(std::strlen(m_code) != 6);
  if (Widgets::Button("Join the room", {half, 0})) {
    SyncLibrary();
    m_client.JoinLobby(m_code);
  }
  ImGui::EndDisabled();
}

void PlayMenu::DrawSoloSetup() {
  ImGui::TextWrapped("Best of three against a dummy that stands still, using "
                     "your match decks for each round.");
  Widgets::SectionHeader("Maps");
  m_maps.DrawPoolPicker(m_soloPool);
  ImGui::Spacing();
  if (Widgets::Button("Begin the duel", {-1, 0}, true)) {
    m_practice = m_maps.BuildOptions(m_soloPool);
    m_open = false;
  }
}

void PlayMenu::DrawRoomSetup() {
  ImGui::Indent();
  m_maps.DrawPoolPicker(m_roomPool);
  ImGui::Unindent();
  if (Widgets::Button("Open the room", {-1, 0}, true)) {
    SyncLibrary();
    m_client.CreateLobby(m_maps.BuildOptions(m_roomPool));
    m_roomSetup = false;
  }
}

void PlayMenu::DrawHistory() {
  if (m_client.GetPhase() == LockstepClient::Phase::Offline) {
    ImGui::TextDisabled("Connect to a server (Online Duel) to see your matches.");
    return;
  }
  if (Widgets::Button(m_history ? "Refresh" : "Load my matches") &&
      !m_historyRequest.valid())
    m_historyRequest = NetClient::GetJsonAsync(ApiBase() + "/players/me/matches",
                                               m_client.Token());
  if (m_historyRequest.valid()) {
    ImGui::SameLine();
    ImGui::TextDisabled("loading...");
  }
  if (!m_history || !m_history->is_array())
    return;
  if (m_history->empty()) {
    ImGui::TextDisabled("No matches yet");
    return;
  }

  int wins = 0, losses = 0;
  for (const auto &m : *m_history) {
    if (m.value("status", "") == "Running")
      continue;
    (m.value("won", false) ? wins : losses) += !m.value("draw", false);
  }
  ImGui::Text("Last %d matches: %d won, %d lost", (int)m_history->size(), wins,
              losses);

  if (ImGui::BeginTable("history", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                        {0, 180})) {
    ImGui::TableSetupColumn("Match");
    ImGui::TableSetupColumn("Result");
    ImGui::TableSetupColumn("Rounds");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (const auto &m : *m_history) {
      int64_t id = m.value("matchId", int64_t{0});
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("#%lld %s", (long long)id, m.value("mode", "").c_str());
      ImGui::TableNextColumn();
      std::string status = m.value("status", "");
      if (status == "Running")
        ImGui::TextDisabled("running");
      else if (status == "Voided")
        ImGui::TextDisabled("void");
      else if (m.value("draw", false))
        ImGui::Text("draw");
      else if (m.value("won", false))
        ImGui::TextColored(Theme::Vec(Tone::Moss), "won%s",
                           status == "Forfeit" ? " (forfeit)" : "");
      else
        ImGui::TextColored(Theme::Vec(Tone::Oxblood), "lost%s",
                           status == "Forfeit" ? " (forfeit)" : "");
      ImGui::TableNextColumn();
      if (m.contains("roundsWon") && m["roundsWon"].size() == 2) {
        int slot = m.value("slot", 0);
        ImGui::Text("%d - %d", m["roundsWon"][slot].get<int>(),
                    m["roundsWon"][1 - slot].get<int>());
      }
      ImGui::TableNextColumn();
      ImGui::PushID((int)id);
      if (status != "Running" && Widgets::SmallButton("Replay") &&
          !m_replayRequest.valid())
        m_replayRequest = NetClient::GetJsonAsync(
            ApiBase() + "/matches/" + std::to_string(id) + "/replay",
            m_client.Token());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}
