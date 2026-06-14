#include "raylib.h"
#include "whas/constants.h"
#include "whas/engine/renderer.h"
#include "whas/engine/simulation.h"
#include "whas/ui/ui.h"

int main() {
  InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Witch Hat Atelier Simulator");
  SetTargetFPS(FPS);

  Simulation sim;
  Renderer renderer;
  UI ui;
  UIState uiState;

  while (!WindowShouldClose()) {
    float dt = GetFrameTime();

    // Input handling
    ui.HandleInput(uiState);

    // World painting via mouse.
    if (!ui.IsMouseOverPanel()) {
      Vector2 cell = ui.GetMouseCell();
      int cx = (int)cell.x;
      int cy = (int)cell.y;

      if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        sim.Paint(cx, cy, uiState.selectedMaterial, uiState.brushRadius);

      if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        sim.Erase(cx, cy, uiState.brushRadius);
    }

    // Simulation tick.
    bool isPainting = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui.IsMouseOverPanel();
    sim.Update(dt, isPainting);

    // Rendering
    BeginDrawing();

    // Draw sky color
    ClearBackground(Color{15, 15, 20, 255});

    // Actual drawing of the world
    renderer.DrawWorld(sim);

    // Draw bebugging layer if needed
    if (uiState.debugOverlay)
      renderer.DrawDebugOverlay(sim);

    ui.Draw(uiState, sim);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
