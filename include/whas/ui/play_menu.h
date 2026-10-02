#pragma once
#include "whas/game/replay_store.h"
#include "whas/net/lockstep_client.h"
#include "imgui.h"
#include "whas/ui/map_gallery.h"
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class UI;

// The main menu: a sidebar down the left edge with a page beside it. Solo
// duels against the dummy, online duels (quick match, rooms with a map
// pool, rejoining), replays and settings; the sandbox, the spell library and
// the maps open straight from the sidebar.
class PlayMenu {
public:
  PlayMenu(LockstepClient &client, UI &ui, MapGallery &maps)
      : m_client(client), m_ui(ui), m_maps(maps) {}

  void Toggle() { m_open = !m_open; }
  void Open() { m_open = true; }
  void Close() { m_open = false; }
  bool IsOpen() const { return m_open; }

  // Inside the ImGui frame (UI overlay)
  void Draw();

  // A solo match with these options was asked for
  std::optional<MatchOptions> TakePracticeRequest() {
    return std::exchange(m_practice, {});
  }
  bool TakeSandboxRequest() { return std::exchange(m_sandbox, false); }
  std::optional<nlohmann::json> TakeReplay() { return std::exchange(m_replay, {}); }

private:
  enum class Page { None, Solo, Online, Replays, Settings };
  void DrawSidebar(float width, float height);
  void DrawPage(ImVec2 pos, float width);
  void DrawOnline();
  void DrawSoloSetup();
  void DrawRoomSetup();
  void DrawReplays();
  void KeepReplays();
  void SyncLibrary();
  std::string ApiBase() const;

  LockstepClient &m_client;
  UI &m_ui;
  MapGallery &m_maps;
  bool m_open = true;
  std::optional<MatchOptions> m_practice;
  Page m_page = Page::Solo;
  bool m_roomSetup = false;
  MapGallery::Pool m_soloPool{MapGallery::RANDOM};
  MapGallery::Pool m_roomPool{MapGallery::RANDOM};
  bool m_roomChaos = false;
  bool m_sandbox = false;
  char m_url[128] = "ws://localhost:8080/ws";
  char m_code[8]{};

  std::optional<nlohmann::json> m_replay; // to watch
  // Saving the last finished match's replay
  std::future<std::optional<nlohmann::json>> m_replayDownload;
  int64_t m_savedMatch = 0;
  int m_saveSlot = 0;
  int m_saveWinner = -1;
  std::vector<ReplayStore::Entry> m_replays;
  bool m_replaysLoaded = false;
  std::string m_confirmDelete;
  std::string m_status;
};
