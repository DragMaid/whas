#pragma once
#include "whas/campaign/campaign.h"
#include "whas/campaign/enemies.h"
#include "whas/game/flight_trail.h"
#include "whas/game/placement.h"
#include "whas/game/rts.h"
#include "whas/spell/spell_editor.h"
#include "whas/spell/spell_library.h"
#include <future>
#include <map>
#include <memory>
#include <random>
#include <string>

class Simulation;
class SvgLibrary;
class UI;
struct UIState;

// A gate, workbench or shrine where it stands (lit: an opened gate, or a
// shrine still to be taken). Placeholder art; the editor draws them too.
void DrawCampaignObject(const Campaign::ObjectDef &o, bool lit,
                        const SvgLibrary &glyphs);

// Playing a campaign: one room in the world at a time, walk off an edge
// into the next. Rooms next to the current one are read (and their
// backgrounds decoded) on a worker thread ahead of time, so crossing over
// only has to build the terrain. Rooms reset whenever they are entered.
class CampaignPlay {
public:
  // A new game (wipes progress) or carrying on from the save
  bool Start(Simulation &sim, UI &ui, const Campaign::CampaignDef &def,
             bool newGame, std::string &error);
  // From the editor: this room, every glyph, nothing saved
  bool StartTest(Simulation &sim, UI &ui, const Campaign::CampaignDef &def,
                 Campaign::RoomPos room, Vector2 at, std::string &error);
  void Stop();
  bool Active() const { return m_active; }
  // Back to the menu (or the editor, after a test)
  bool TakeExit() { return std::exchange(m_exit, false); }
  bool Testing() const { return m_testing; }

  void Update(Simulation &sim, UI &ui, UIState &state);
  // Behind the world's cells
  void DrawBackground() const;
  // Inside the world camera
  void DrawWorld(const Simulation &sim, const UI &ui) const;
  // Inside the ImGui frame: HUD, backpack, gate map, workbench
  void DrawPanels(UI &ui);

private:
  struct Loaded {
    Campaign::RoomDef room;
    Image background{}; // decoded off the main thread; uploaded on entry
  };
  using Pending = std::shared_future<std::shared_ptr<Loaded>>;

  void Preload(Campaign::RoomPos pos);
  std::shared_ptr<Loaded> Fetch(Campaign::RoomPos pos);
  void EnterRoom(Simulation &sim, Campaign::RoomPos pos, Vector2 at,
                 bool keepMotion);
  void Respawn(Simulation &sim);
  void Tick(Simulation &sim, UI &ui, CharacterInput input, float dt);
  void CheckEdges(Simulation &sim, const CharacterInput &input);
  void Interact(Simulation &sim);
  void Cast(Simulation &sim, UI &ui);
  void SaveProgress();
  void Notify(std::string text, float seconds = 2.5f);
  const Spell *SlotSpell(int slot) const;
  bool PanelOpen() const { return m_backpack || m_warp || m_bench.IsOpen(); }
  int NearObject(Campaign::ObjectKind kind) const;

  void DrawHud(UI &ui);
  void DrawBackpack(UI &ui);
  void DrawWarp(Simulation *sim);

  bool m_active = false;
  bool m_testing = false;
  bool m_exit = false;
  Campaign::CampaignDef m_def;
  Campaign::Save m_save;
  Campaign::RoomDef m_room;
  Texture2D m_background{};
  std::map<Campaign::RoomPos, Pending> m_cache;

  Character m_player;
  std::vector<Enemies::Enemy> m_enemies;
  FlightTrail m_trail;
  CastTargeting m_targeting;
  Rts::Cooldowns m_cooldowns;
  int m_tick = 0;
  int m_slot = 0;
  float m_accumulator = 0.0f;
  float m_deadFor = 0.0f;
  std::mt19937 m_rng{12345};

  SpellLibrary m_backpackSpells;
  SpellEditor m_bench;
  bool m_benchWasOpen = false;
  bool m_backpack = false;
  bool m_warp = false;
  Simulation *m_warpSim = nullptr;

  std::string m_notice;
  float m_noticeTime = 0.0f;
};
