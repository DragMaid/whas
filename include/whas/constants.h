#pragma once

constexpr int FPS = 60;

constexpr int WINDOW_WIDTH    = 1280;
constexpr int WINDOW_HEIGHT   = 720;
constexpr int CELL_SIZE       = 4;
constexpr int GRID_W          = WINDOW_WIDTH  / CELL_SIZE;
constexpr int GRID_H          = WINDOW_HEIGHT / CELL_SIZE;

constexpr int CHUNK_SIZE      = 32;
constexpr int CHUNK_COLS      = (GRID_W + CHUNK_SIZE - 1) / CHUNK_SIZE;
constexpr int CHUNK_ROWS      = (GRID_H + CHUNK_SIZE - 1) / CHUNK_SIZE;

// Water Specific Physical Thresholds (Phase Transitions)
// These remain constexpr as they define the material's identity/state changes
// while the movement/physics properties are tunable in the config.
constexpr float WATER_BOILING_POINT = 100.0f;
constexpr float WATER_FREEZING_POINT = 0.0f;
