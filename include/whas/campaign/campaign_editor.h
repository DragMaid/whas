#pragma once
#include "whas/campaign/campaign.h"
#include <optional>
#include <raylib.h>
#include <string>
#include <vector>

class Simulation;
class UI;

// The campaign list (play, continue, edit) and the campaign editor. The
// editor works on one room at a time in the live world, like the map
// editor: paint its terrain, give it a background, place gates,
// workbenches, glyph shrines and enemies, and grow the campaign with '+'
// beside the selected room. Rooms are saved as you move between them.
class CampaignEditor {
public:
  void OpenHub();
  void CloseHub() { m_hub = false; }
  bool HubOpen() const { return m_hub; }
  bool Editing() const { return m_editing; }

  // Mouse in the world, file drops, letting the world settle
  void Update(Simulation &sim);
  void DrawBackground() const;
  void DrawWorld(const UI &ui) const;
  // Inside the ImGui frame: the list, or the editor's panel
  void DrawPanel(Simulation &sim, UI &ui);

  struct PlayRequest {
    Campaign::CampaignDef def;
    bool newGame = false;
  };
  std::optional<PlayRequest> TakePlay() { return std::exchange(m_play, {}); }
  struct TestRequest {
    Campaign::CampaignDef def;
    Campaign::RoomPos room;
    Vector2 at;
  };
  std::optional<TestRequest> TakeTest() { return std::exchange(m_test, {}); }
  // Back from a test run: put the room being edited back in the world
  void Resume(Simulation &sim);
  // Save the room and leave the editor
  void Close(Simulation &sim);

private:
  enum class Tool {
    Paint,
    Select,
    Gate,
    Workbench,
    Shrine,
    Start,
    Mage,
    Undead,
    Flyer,
  };
  struct Selection {
    enum Kind { None, Object, Enemy, Start } kind = None;
    int index = -1;
  };

  void Reload();
  void Open(Simulation &sim, const Campaign::CampaignDef &def);
  void LoadRoomIntoWorld(Simulation &sim, Campaign::RoomPos pos);
  bool StoreRoom(Simulation &sim);
  void AddRoom(Simulation &sim, int dx, int dy);
  void DeleteRoom(Simulation &sim);
  void SetBackground(const std::string &file);
  Selection HitTest(Vector2 cell) const;
  void Place(const Simulation &sim, Vector2 cell);
  void RemoveSelected();

  void DrawHub();
  void DrawRoomMap(Simulation &sim);
  void DrawTools();
  void DrawPaint(Simulation &sim);
  void DrawSelected(UI &ui);
  void DrawRoomSettings();
  void DrawStartingKit(UI &ui);

  bool m_hub = false;
  bool m_editing = false;
  std::vector<Campaign::CampaignDef> m_campaigns;
  char m_newName[33]{};
  std::string m_confirm; // id waiting for "really?" (delete / new game)
  bool m_confirmDelete = false;

  Campaign::CampaignDef m_def;
  Campaign::RoomDef m_room;
  Texture2D m_background{};
  char m_name[33]{};
  char m_bgPath[512]{};
  std::string m_status;

  Tool m_tool = Tool::Paint;
  Selection m_selected;
  bool m_dragging = false;
  Vector2 m_dragOffset{0, 0};
  Element m_brushElement = Element::EARTH;
  int m_brush = 3;
  bool m_running = false;
  float m_accumulator = 0.0f;
  std::vector<MapDef> m_maps; // 1v1 maps to copy terrain from
  int m_mapPick = 0;

  std::optional<PlayRequest> m_play;
  std::optional<Campaign::CampaignDef> m_editRequest;
  std::optional<TestRequest> m_test;
};
