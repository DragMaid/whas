#include "whas/engine/view.h"
#include "whas/ui/play_menu.h"
#include "whas/game/replay_store.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/ui/ui.h"
#include "whas/ui/audio_settings.h"
#include "whas/spell/spell_rules.h"
#include "whas/ui/theme.h"
#include "whas/ui/widgets.h"
#include <cfloat>
#include <chrono>
#include <ctime>
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
  // Spells over the sign limit stay home: ordinary matches can't use them
  for (const Spell &s : spells.All())
    if (SpellRules::WithinLimits(s))
      lib.spells.push_back({spells.RefOf(s), s});
  lib.decks = m_ui.Decks().Decks();
  lib.match = m_ui.Decks().Match();
  m_client.SetLibrary(std::move(lib));
}

void PlayMenu::KeepReplays() {
  // Every finished online match is downloaded once and kept on this machine
  int64_t id = m_client.MatchId();
  if (m_client.GetPhase() == LockstepClient::Phase::MatchOver && id != 0 &&
      id != m_savedMatch && !m_replayDownload.valid()) {
    m_savedMatch = id;
    m_saveSlot = m_client.Slot();
    m_saveWinner = m_client.MatchWinner();
    m_replayDownload = NetClient::GetJsonAsync(
        ApiBase() + "/matches/" + std::to_string(id) + "/replay", m_client.Token());
  }
  if (!Ready(m_replayDownload))
    return;
  std::optional<nlohmann::json> replay = m_replayDownload.get();
  std::string error;
  if (replay && ReplayStore::Save(std::move(*replay), m_saveSlot, m_saveWinner, error)) {
    m_status = "Replay saved";
    m_replaysLoaded = false;
  } else {
    m_status = "Couldn't download the replay" + (error.empty() ? "" : ": " + error);
  }
}

void PlayMenu::Draw() {
  KeepReplays();
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
    DrawReplays();
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
  for (int r = 0; r < MATCH_ROUNDS; ++r) {
    const Deck *d = m_ui.Decks().Find(rounds.deckIds[r]);
    if (!d)
      continue;
    int over = 0;
    for (const std::string &ref : d->slots)
      if (const Spell *s = m_ui.Library().Find(ref); s && !SpellRules::WithinLimits(*s))
        ++over;
    if (over > 0)
      ImGui::TextColored(Theme::Vec(Tone::Oxblood),
                         "Round %d: %d spell%s over the sign limit will be left out",
                         r + 1, over, over == 1 ? "" : "s");
  }

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

void PlayMenu::DrawReplays() {
  if (!m_replaysLoaded) {
    m_replays = ReplayStore::List();
    m_replaysLoaded = true;
  }
  ImGui::TextWrapped("Every online match you finish is saved here, on this "
                     "machine only.");
  if (m_replays.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("No replays yet.");
    return;
  }
  int wins = 0, losses = 0;
  for (const ReplayStore::Entry &e : m_replays) {
    wins += e.result > 0;
    losses += e.result < 0;
  }
  ImGui::TextDisabled("%d saved  -  %d won, %d lost", (int)m_replays.size(), wins,
                      losses);
  ImGui::Spacing();

  float height = std::min(420.0f * View::UiScale(),
                          (m_replays.size() + 1) * ImGui::GetFrameHeightWithSpacing() + 8);
  if (!ImGui::BeginTable("replays", 5,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_BordersInnerH,
                         {0, height}))
    return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Played");
  ImGui::TableSetupColumn("Against");
  ImGui::TableSetupColumn("Result");
  ImGui::TableSetupColumn("Maps");
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f * View::UiScale());
  ImGui::TableHeadersRow();
  std::string removed;
  for (const ReplayStore::Entry &e : m_replays) {
    ImGui::PushID(e.path.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    std::time_t when = static_cast<std::time_t>(e.savedAt);
    char date[32] = "-";
    if (e.savedAt > 0)
      std::strftime(date, sizeof date, "%d %b %H:%M", std::localtime(&when));
    ImGui::TextUnformatted(date);
    ImGui::TableNextColumn();
    ImGui::Text("guest #%lld", (long long)e.opponentId);
    ImGui::TableNextColumn();
    Tone tone = e.result > 0 ? Tone::Moss : e.result < 0 ? Tone::Oxblood : Tone::Muted;
    ImGui::TextColored(Theme::Vec(tone), "%s  %d-%d",
                       e.result > 0 ? "won" : e.result < 0 ? "lost" : "draw",
                       e.roundsWon[e.slot], e.roundsWon[1 - e.slot]);
    ImGui::TableNextColumn();
    std::string maps;
    for (const std::string &m : e.maps)
      maps += (maps.empty() ? "" : ", ") + m;
    ImGui::TextDisabled("%s", maps.empty() ? "Random arenas" : maps.c_str());
    ImGui::TableNextColumn();
    if (Widgets::SmallButton("Watch")) {
      m_replay = ReplayStore::Load(e.path);
      if (m_replay)
        m_open = false;
      else
        m_status = "Couldn't read that replay";
    }
    ImGui::SameLine();
    if (m_confirmDelete == e.path) {
      if (Widgets::SmallButton("Sure?"))
        removed = e.path;
    } else if (Widgets::SmallButton("Delete")) {
      m_confirmDelete = e.path;
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
  if (!removed.empty()) {
    ReplayStore::Remove(removed);
    m_confirmDelete.clear();
    m_replaysLoaded = false;
  }
}
