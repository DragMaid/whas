#include "raylib.h"
#include "whas/constants.h"
#include "whas/engine/renderer.h"
#include "whas/engine/simulation.h"
#include "whas/game/game.h"
#include "whas/game/sandbox.h"
#include "whas/ui/ui.h"

int main() {
  SetConfigFlags(FLAG_MSAA_4X_HINT); // smooth vector lines
  InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Witch Hat Atelier Simulator");
  SetExitKey(KEY_NULL); // Escape closes the spell editor, not the game
  SetTargetFPS(FPS);

  Simulation sim;
  Renderer renderer;
  UI ui;
  UIState uiState;
  Game game;
  Sandbox sandbox;

  while (!WindowShouldClose()) {
    ui.HandleInput(uiState, sim);

    if (IsKeyPressed(KEY_F1))
      game.SetActive(!game.IsActive(), sim, ui);
    // "Test in sandbox" from the spell editor
    if (uiState.sandboxRequested) {
      uiState.sandboxRequested = false;
      if (game.IsActive())
        game.SetActive(false, sim, ui);
    }

    // A match drives the world in turns; the sandbox runs it in real time
    if (game.IsActive())
      game.Update(sim, ui, uiState);
    else
      sandbox.Update(sim, ui, uiState);

    BeginDrawing();
    ClearBackground(Color{15, 15, 20, 255});
    renderer.DrawWorld(sim);

    if (game.IsActive())
      game.Draw(sim, ui);
    else
      sandbox.Draw(sim, ui, uiState);

    if (uiState.debugOverlay)
      renderer.DrawDebugOverlay(sim);

    ui.Draw(uiState, sim);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
