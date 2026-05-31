#pragma once

constexpr int FPS = 60;

constexpr int WINDOW_WIDTH    = 1280;
constexpr int WINDOW_HEIGHT   = 720;
constexpr int CELL_SIZE       = 4;
constexpr int GRID_W          = WINDOW_WIDTH  / CELL_SIZE;
constexpr int GRID_H          = WINDOW_HEIGHT / CELL_SIZE;

constexpr int CHUNK_SIZE      = 32;
// NOTE: arithmetic ceiling - instead of simple division
constexpr int CHUNK_COLS      = (GRID_W + CHUNK_SIZE - 1) / CHUNK_SIZE;
constexpr int CHUNK_ROWS      = (GRID_H + CHUNK_SIZE - 1) / CHUNK_SIZE;

constexpr float GRAVITY       = 0.3f;
constexpr float HEAT_DIFFUSE  = 0.02f;
constexpr float PRESSURE_EQ   = 0.15f;

// TODO: find out if theres any more way to write config in cpp ?
