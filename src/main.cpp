#include "raylib.h"
#include "whas/audio/audio_manager.h"
#include "whas/audio/audio_observer.h"
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
#include "whas/ui/play_menu.h"
#include "whas/ui/ui.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

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
  PlayMenu menu(client, ui);
  if (const char *server = Arg(argc, argv, "--server")) {
    client.Connect(server);
  }

  ui.SetOverlay([&] {
    menu.Draw();
    if (replay.Active())
      replay.DrawControls(sim);
  });

  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_F11))
      ToggleBorderlessWindowed();
    View::Update();
    ui.HandleInput(uiState, sim);

    if (uiState.menuRequested) {
      uiState.menuRequested = false;
      menu.Toggle();
    }
    if (menu.TakeSandboxRequest() || uiState.sandboxRequested) {
      uiState.sandboxRequested = false;
      replay.Close();
      if (game.IsOnline())
        client.Leave();
      else if (game.IsActive())
        game.SetActive(false, sim, ui);
    }
    if (menu.TakePracticeRequest() && !game.IsOnline()) {
      replay.Close();
      game.SetActive(false, sim, ui);
      game.SetActive(true, sim, ui);
      game.StartMatch(sim, GetRandomValue(1, 1 << 30));
    }
    if (auto stored = menu.TakeReplay(); stored && !game.IsOnline()) {
      if (game.IsActive())
        game.SetActive(false, sim, ui);
      replay.Open(*stored, sim);
    }
    if (IsKeyPressed(KEY_F1) && !game.IsOnline() && !replay.Active())
      game.SetActive(!game.IsActive(), sim, ui);

    // Online: a match found (or rejoined) takes over the screen
    if (client.InMatch() && !game.IsOnline()) {
      replay.Close();
      menu.Close();
      game.StartOnline(sim, ui, client);
    }
    uiState.configLocked = game.IsOnline();

    // A match drives the world in turns, the sandbox in real time, a replay
    // from its recording
    if (replay.Active()) {
      replay.Update(sim, uiState);
    } else if (game.IsActive()) {
      game.Update(sim, ui, uiState);
      if (game.TakeExitRequest()) {
        game.LeaveOnline(ui);
        menu.Open();
      }
    } else {
      // Keep the connection serviced while not in a match
      client.Update(sim, game.NetState());
      sandbox.Update(sim, ui, uiState);
    }
    audio.Update();
    soundscape.Update(sim, audio, GetFrameTime());

    BeginDrawing();
    ClearBackground(Color{8, 8, 11, 255}); // the bars beside the world
    BeginMode2D(View::Camera());
    DrawRectangle(0, 0, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE,
                  Color{15, 15, 20, 255});
    renderer.DrawWorld(sim);

    if (replay.Active())
      replay.Draw();
    else if (game.IsActive())
      game.Draw(sim, ui);
    else
      sandbox.Draw(sim, ui, uiState);

    ui.DrawWorld(uiState, sim);
    EndMode2D();

    if (uiState.debugOverlay)
      renderer.DrawDebugOverlay(sim);
    ui.Draw(uiState, sim);
    EndDrawing();
  }

  client.Leave();
  UnloadCharacterSprites();
  audio.Shutdown();
  CloseAudioDevice();
  CloseWindow();
  return 0;
}
