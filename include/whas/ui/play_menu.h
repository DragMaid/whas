#pragma once
#include "whas/net/lockstep_client.h"
#include "whas/ui/map_gallery.h"
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class UI;

// The "Play" window: practice against the dummy, connect to a server and
// find a match (quick match or lobby code), rejoin a dropped match, browse
// past matches and open their replays.
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
  void DrawOnline();
  void DrawSoloSetup();
  void DrawRoomSetup();
  void DrawHistory();
  void SyncLibrary();
  std::string ApiBase() const;

  LockstepClient &m_client;
  UI &m_ui;
  MapGallery &m_maps;
  bool m_open = true;
  std::optional<MatchOptions> m_practice;
  bool m_soloSetup = false;
  bool m_roomSetup = false;
  MapGallery::Pool m_soloPool{MapGallery::RANDOM};
  MapGallery::Pool m_roomPool{MapGallery::RANDOM};
  bool m_sandbox = false;
  char m_url[128] = "ws://localhost:8080/ws";
  char m_code[8]{};

  std::future<std::optional<nlohmann::json>> m_historyRequest;
  std::optional<nlohmann::json> m_history;
  std::future<std::optional<nlohmann::json>> m_replayRequest;
  std::optional<nlohmann::json> m_replay;
  std::string m_status;
};
