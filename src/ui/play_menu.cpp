#include "whas/engine/view.h"
#include "whas/ui/play_menu.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/ui/ui.h"
#include <chrono>
#include <cstring>

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

  ImGui::SetNextWindowPos({GetScreenWidth() * 0.5f, 120.0f * View::UiScale()}, ImGuiCond_Appearing,
                          {0.5f, 0.0f});
  if (!ImGui::Begin("Play", &m_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  if (ImGui::Button("Sandbox", {130, 0})) {
    m_sandbox = true;
    m_open = false;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Paint the world and test spells from your avatar");
  ImGui::SameLine();
  if (ImGui::Button("Practice match", {130, 0})) {
    m_practice = true;
    m_open = false;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Best of 3 against a dummy that stands still,\n"
                      "using your match decks for each round");
  ImGui::SameLine();
  if (ImGui::Button("Spells & decks", {130, 0})) {
    m_ui.OpenSpellLibrary();
    m_open = false;
  }

  ImGui::SeparatorText("Online");
  DrawOnline();

  if (!m_status.empty())
    ImGui::TextColored({1, 0.7f, 0.4f, 1}, "%s", m_status.c_str());
  ImGui::End();
}

void PlayMenu::DrawOnline() {
  using P = LockstepClient::Phase;
  P phase = m_client.GetPhase();
  bool offline = phase == P::Offline;

  ImGui::BeginDisabled(!offline);
  ImGui::SetNextItemWidth(260);
  ImGui::InputText("Server", m_url, sizeof m_url);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (offline) {
    if (ImGui::Button("Connect"))
      m_client.Connect(m_url);
  } else if (ImGui::Button("Disconnect")) {
    m_client.Disconnect();
  }
  ImGui::TextDisabled("Status: %s", PhaseName(phase));
  if (m_client.PlayerId() != 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("  (you are guest #%lld)", (long long)m_client.PlayerId());
  }

  if (phase == P::Queued) {
    if (ImGui::Button("Stop looking"))
      m_client.CancelWaiting();
    return;
  }
  if (phase == P::Hosting) {
    ImGui::Text("Your lobby code:");
    ImGui::SameLine();
    ImGui::TextColored({1, 0.85f, 0.4f, 1}, "%s", m_client.LobbyCode().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
      ImGui::SetClipboardText(m_client.LobbyCode().c_str());
    if (ImGui::Button("Close lobby"))
      m_client.CancelWaiting();
    return;
  }
  if (phase != P::Ready && phase != P::MatchOver)
    return;

  const MatchDecks &rounds = m_ui.Decks().Match();
  ImGui::TextDisabled("Rounds 1-3 use:");
  for (int r = 0; r < MATCH_ROUNDS; ++r) {
    const Deck *d = m_ui.Decks().Find(rounds.deckIds[r]);
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", d ? d->name.c_str() : "?",
                        r + 1 < MATCH_ROUNDS ? "," : "");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("change"))
    m_ui.OpenSpellLibrary();

  if (auto running = m_client.RunningMatch()) {
    ImGui::TextColored({1, 0.8f, 0.3f, 1}, "You have a match in progress (#%lld)",
                       (long long)*running);
    if (ImGui::Button("Rejoin match", {200, 0}))
      m_client.Rejoin();
    return;
  }

  if (ImGui::Button("Quick match", {130, 0})) {
    SyncLibrary();
    m_client.QuickMatch();
  }
  ImGui::SameLine();
  if (ImGui::Button("Create lobby", {130, 0})) {
    SyncLibrary();
    m_client.CreateLobby();
  }
  ImGui::SetNextItemWidth(90);
  ImGui::InputTextWithHint("##code", "CODE", m_code, sizeof m_code,
                           ImGuiInputTextFlags_CharsUppercase);
  ImGui::SameLine();
  ImGui::BeginDisabled(std::strlen(m_code) != 6);
  if (ImGui::Button("Join lobby")) {
    SyncLibrary();
    m_client.JoinLobby(m_code);
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("History");
  DrawHistory();
}

void PlayMenu::DrawHistory() {
  if (ImGui::Button(m_history ? "Refresh" : "Load my matches") &&
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
        ImGui::TextColored({0.5f, 0.9f, 0.5f, 1}, "won%s",
                           status == "Forfeit" ? " (forfeit)" : "");
      else
        ImGui::TextColored({0.95f, 0.45f, 0.4f, 1}, "lost%s",
                           status == "Forfeit" ? " (forfeit)" : "");
      ImGui::TableNextColumn();
      if (m.contains("roundsWon") && m["roundsWon"].size() == 2) {
        int slot = m.value("slot", 0);
        ImGui::Text("%d - %d", m["roundsWon"][slot].get<int>(),
                    m["roundsWon"][1 - slot].get<int>());
      }
      ImGui::TableNextColumn();
      ImGui::PushID((int)id);
      if (status != "Running" && ImGui::SmallButton("Replay") &&
          !m_replayRequest.valid())
        m_replayRequest = NetClient::GetJsonAsync(
            ApiBase() + "/matches/" + std::to_string(id) + "/replay",
            m_client.Token());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}
