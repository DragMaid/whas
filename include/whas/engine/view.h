#pragma once
#include <raylib.h>

// How the fixed-size world (GRID_W x GRID_H cells, drawn CELL_SIZE pixels
// each) fits the window, whatever size the window is. The world keeps its
// size so every player simulates the same arena; it's scaled to fit and
// centred, with bars on the sides that don't fit. World drawing happens
// inside BeginMode2D(Camera()), in "world pixels"; the UI draws in screen
// pixels on top.
namespace View {

// Screen pixels kept clear around the world (editors put their toolbars
// and panels there); the world fits into what's left
struct Insets {
  float left = 0, top = 0, right = 0, bottom = 0;
};
void SetInsets(Insets insets);

// Refit to the current window size; call once a frame before drawing
void Update();

Camera2D Camera();
// Screen pixels per world pixel
float Scale();

// The mouse in world pixels and in cells
Vector2 MouseWorld();
Vector2 MouseCells();

// A world pixel position on screen
Vector2 WorldToScreen(Vector2 world);

// How much bigger the UI is drawn than at 720p (1 to 2.5, in quarter
// steps so resizing doesn't restyle every frame)
float UiScale();

} // namespace View
