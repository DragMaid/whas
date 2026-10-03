#include "imgui.h"
#include "raylib.h"
#include "whas/audio/audio_manager.h"
#include "whas/audio/audio_observer.h"
#include "whas/campaign/campaign_editor.h"
#include "whas/campaign/campaign_play.h"
#include "whas/constants.h"
#include "whas/engine/renderer.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/character_draw.h"
#include "whas/game/game.h"
#include "whas/game/replay.h"
#include "whas/game/replay_view.h"
#include "whas/game/sandbox.h"
#include "whas/net/lockstep_client.h"
#include "whas/ui/grimoire_panel.h"
#include "whas/ui/map_editor.h"
#include "whas/ui/map_gallery.h"
#include "whas/ui/play_menu.h"
#include "whas/ui/ui.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

// whasg --replay match.json [--verify]: re-simulate a stored match without a
// window and check every turn against the hashes the players reported
static int VerifyReplay(const char *path) {
  std::ifstream file(path);
  auto replay = nlohmann::json::parse(file, nullptr, false);
  if (!file || replay.is_discarded()) {
    std::fprintf(stderr, "can't read %s\n", path);
    return 2;
  }
  ReplayPlayer player;
  std::string error;
  if (!player.Load(replay, error)) {
    std::fprintf(stderr, "bad replay: %s\n", error.c_str());
    return 2;
  }
  if (player.BuildId() != WHAS_SIM_BUILD_ID)
    std::fprintf(stderr, "note: recorded on build %s, this is %s\n",
                 player.BuildId().c_str(), WHAS_SIM_BUILD_ID);
  SetTraceLogLevel(LOG_WARNING);
  Simulation sim;
  std::vector<std::string> report;
  int mismatches = player.VerifyAll(sim, &report);
  for (const std::string &line : report)
    std::printf("MISMATCH %s\n", line.c_str());
  std::printf("match %lld: %d turns, %d hashes checked, %d mismatches\n",
              (long long)player.MatchId(), player.TurnCount(), player.Checked(),
              mismatches);
  return mismatches == 0 ? 0 : 1;
}

static const char *Arg(int argc, char **argv, const char *name) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0)
      return argv[i + 1];
  return nullptr;
}

int main(int argc, char **argv) {
  if (argc >= 3 && std::strcmp(argv[1], "--replay") == 0 &&
      (argc < 4 || std::strcmp(argv[3], "--verify") == 0))
    return VerifyReplay(argv[2]);

  // Smooth vector lines; any window size (the world is scaled to fit)
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Witch Hat Atelier Simulator");
  SetWindowMinSize(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);
  SetExitKey(KEY_NULL); // Escape closes the spell editor, not the game
  SetTargetFPS(FPS);
  InitAudioDevice();

  Simulation sim;
  Renderer renderer;
  UI ui;
  UIState uiState;
  Game game;
  Sandbox sandbox;
  ReplayView replay;
  // Listens to the world; it only reads the simulation, so lockstep peers
  // with and without sound stay in sync
  AudioManager audio;
  audio.Init();
  AudioObserver soundscape;

  // --identity lets two copies on one machine play as different guests
  LockstepClient client;
  if (const char *identity = Arg(argc, argv, "--identity"))
    client.SetIdentityFile(identity);
  MapGallery maps;
  MapEditor mapEditor(maps.Thumbnails());
  CampaignEditor campaignEditor;
  CampaignPlay campaign;
  std::string campaignError;
  PlayMenu menu(client, ui, maps);
  GrimoirePanel grimoire(ui);
  if (const char *server = Arg(argc, argv, "--server")) {
    client.Connect(server);
  }

  ui.SetOverlay([&] {
    menu.Draw();
    maps.Draw();
    mapEditor.DrawPanel(sim);
    if (!campaign.Active())
      campaignEditor.DrawPanel(sim, ui);
    campaign.DrawPanels(ui);
    if (!campaignError.empty()) {
      ImGui::OpenPopup("Campaign");
      if (ImGui::BeginPopupModal("Campaign", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", campaignError.c_str());
        if (ImGui::Button("OK")) {
          campaignError.clear();
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    if (replay.Active())
      replay.DrawControls(sim);
    // The decks the players brought: after an online match, or beside a
    // replay, so spells can be copied
    if (game.IsOnline() &&
        client.GetPhase() == LockstepClient::Phase::MatchOver)
      grimoire.Draw(client.AllCards(), client.Slot());
    else if (replay.Active() && replay.ShowSpells())
      grimoire.Draw(replay.Player().Cards(), replay.Player().LocalSlot());
    else
      grimoire.Reset();
  });

  // --open sandbox|duel|rts|spells|spell-editor|maps|map-editor|campaigns|campaign:<id>
  // starts on that screen
  bool startDuel = false, startRts = false;
  if (const char *screen = Arg(argc, argv, "--open")) {
    if (std::strcmp(screen, "sandbox") == 0) {
      menu.Close();
    } else if (std::strcmp(screen, "spell-editor") == 0) {
      menu.Close();
      ui.OpenSpellEditor();
    } else if (std::strcmp(screen, "spells") == 0) {
      menu.Close();
      ui.OpenSpellLibrary();
    } else if (std::strcmp(screen, "duel") == 0 ||
               std::strcmp(screen, "rts") == 0) {
      menu.Close();
      startDuel = true;
      startRts = std::strcmp(screen, "rts") == 0;
    } else if (std::strcmp(screen, "maps") == 0) {
      menu.Close();
      maps.Open();
    } else if (std::strcmp(screen, "map-editor") == 0) {
      menu.Close();
      mapEditor.Open(sim);
    } else if (std::strcmp(screen, "campaigns") == 0) {
      menu.Close();
      campaignEditor.OpenHub();
    } else if (std::strncmp(screen, "campaign:", 9) == 0) {
      // campaign:<id> carries on that campaign
      menu.Close();
      for (const Campaign::CampaignDef &def : Campaign::LoadAll())
        if (def.id == screen + 9)
          campaign.Start(sim, ui, def, false, campaignError);
    }
  }

  // --screenshot file.png: save a frame after a second and quit (checking
  // screens without clicking through them)
  const char *screenshot = Arg(argc, argv, "--screenshot");
  int frames = 0;

  // --watch replay.json opens a saved replay
  if (const char *path = Arg(argc, argv, "--watch")) {
    std::ifstream file(path);
    auto stored = nlohmann::json::parse(file, nullptr, false);
    if (file && !stored.is_discarded()) {
      menu.Close();
      replay.Open(stored, sim);
    }
  }

  bool wasEditing = false;
  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_F11))
      ToggleBorderlessWindowed();
    View::Update();
    uiState.keysTaken = campaign.Active() || campaignEditor.Editing();
    ui.HandleInput(uiState, sim);

    if (uiState.menuRequested) {
      uiState.menuRequested = false;
      menu.Toggle();
    }
    // Campaigns: the list, the editor, playing one
    auto leaveOtherModes = [&] {
      replay.Close();
      mapEditor.Close();
      maps.Close();
      menu.Close();
      if (game.IsActive() && !game.IsOnline())
        game.SetActive(false, sim, ui);
    };
    if (menu.TakeCampaignRequest() && !game.IsOnline()) {
      leaveOtherModes();
      campaign.Stop();
      campaignEditor.OpenHub();
    }
    if (auto play = campaignEditor.TakePlay()) {
      leaveOtherModes();
      campaignEditor.CloseHub();
      if (!campaign.Start(sim, ui, play->def, play->newGame, campaignError))
        campaignEditor.OpenHub();
    }
    if (auto test = campaignEditor.TakeTest()) {
      if (!campaign.StartTest(sim, ui, test->def, test->room, test->at, campaignError))
        campaignEditor.Resume(sim);
    }
    if (campaign.TakeExit()) {
      bool testing = campaign.Testing();
      campaign.Stop();
      if (testing)
        campaignEditor.Resume(sim);
      else
        campaignEditor.OpenHub();
    }
    bool campaigning = campaign.Active() || campaignEditor.Editing();

    if (menu.TakeSandboxRequest() || uiState.sandboxRequested) {
      uiState.sandboxRequested = false;
      campaign.Stop();
      if (campaignEditor.Editing())
        campaignEditor.Close(sim);
      campaignEditor.CloseHub();
      campaigning = false;
      replay.Close();
      if (game.IsOnline())
        client.Leave();
      else if (game.IsActive())
        game.SetActive(false, sim, ui);
    }
    // A solo match: from the menu's setup, the gallery or the map editor
    std::optional<MatchOptions> practice = menu.TakePracticeRequest();
    if (std::exchange(startDuel, false)) {
      practice = MatchOptions{};
      practice->rts = startRts;
    }
    if (std::optional<MapDef> map = maps.TakePlay(); map) {
      practice = MatchOptions{};
      practice->pool.push_back({std::move(map)});
    }
    if (std::optional<MapDef> map = mapEditor.TakeTest(); map) {
      practice = MatchOptions{};
      practice->pool.push_back({std::move(map)});
    }
    if (practice && !game.IsOnline()) {
      replay.Close();
      mapEditor.Close();
      game.SetActive(false, sim, ui);
      game.SetActive(true, sim, ui);
      game.StartMatch(sim, GetRandomValue(1, 1 << 30), 0, std::move(*practice));
    }
    if (auto edit = maps.TakeEdit(); edit && !game.IsOnline()) {
      replay.Close();
      menu.Close();
      if (game.IsActive())
        game.SetActive(false, sim, ui);
      mapEditor.Open(sim, std::move(*edit));
    }
    if (mapEditor.TakeSaved())
      maps.Reload();
    // Closing the editor goes back to the gallery
    bool editing = mapEditor.IsOpen();
    if (wasEditing && !editing && !game.IsActive())
      maps.Open();
    wasEditing = editing;
    uiState.hideActionBar = editing || menu.IsOpen() || campaigning ||
                             campaignEditor.HubOpen();
    if (auto stored = menu.TakeReplay(); stored && !game.IsOnline()) {
      if (game.IsActive())
        game.SetActive(false, sim, ui);
      replay.Open(*stored, sim);
    }
    if (IsKeyPressed(KEY_F1) && !game.IsOnline() && !replay.Active() &&
        !mapEditor.IsOpen() && !campaigning)
      game.SetActive(!game.IsActive(), sim, ui);

    // Online: a match found (or rejoined) takes over the screen
    if (client.InMatch() && !game.IsOnline()) {
      campaign.Stop();
      if (campaignEditor.Editing())
        campaignEditor.Close(sim);
      campaignEditor.CloseHub();
      replay.Close();
      mapEditor.Close();
      maps.Close();
      menu.Close();
      game.StartOnline(sim, ui, client);
    }
    uiState.configLocked = game.IsOnline();

    // A match drives the world in turns, the sandbox in real time, a replay
    // from its recording
    if (replay.Active()) {
      replay.Update(sim, uiState);
    } else if (campaign.Active()) {
      client.Update(sim, game.NetState());
      campaign.Update(sim, ui, uiState);
    } else if (campaignEditor.Editing()) {
      client.Update(sim, game.NetState());
      campaignEditor.Update(sim);
    } else if (game.IsActive()) {
      game.Update(sim, ui, uiState);
      if (game.TakeExitRequest()) {
        game.LeaveOnline(ui);
        menu.Open();
      }
    } else {
      // Keep the connection serviced while not in a match
      client.Update(sim, game.NetState());
      if (mapEditor.IsOpen())
        mapEditor.Update(sim);
      else
        sandbox.Update(sim, ui, uiState);
    }
    audio.Update();
    soundscape.Update(sim, audio, GetFrameTime());

    SetCharacterHitboxVisible(uiState.debugOverlay);
    BeginDrawing();
    ClearBackground(Color{8, 8, 11, 255}); // the bars beside the world
    BeginMode2D(View::Camera());
    DrawRectangle(0, 0, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE,
                  Color{15, 15, 20, 255});
    if (campaign.Active())
      campaign.DrawBackground();
    else if (campaignEditor.Editing())
      campaignEditor.DrawBackground();
    renderer.DrawWorld(sim);

    if (replay.Active())
      replay.Draw();
    else if (campaign.Active())
      campaign.DrawWorld(sim, ui);
    else if (campaignEditor.Editing())
      campaignEditor.DrawWorld(ui);
    else if (game.IsActive())
      game.Draw(sim, ui);
    else if (mapEditor.IsOpen())
      mapEditor.DrawWorld();
    else
      sandbox.Draw(sim, ui, uiState);

    ui.DrawWorld(uiState, sim);
    EndMode2D();

    if (uiState.debugOverlay)
      renderer.DrawDebugOverlay(sim);
    ui.Draw(uiState, sim);
    EndDrawing();
    if (screenshot && ++frames == FPS) {
      TakeScreenshot(screenshot);
      break;
    }
  }

  campaign.Stop();
  if (campaignEditor.Editing())
    campaignEditor.Close(sim);
  client.Leave();
  UnloadCharacterSprites();
  audio.Shutdown();
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
