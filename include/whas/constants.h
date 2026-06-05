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

// Physical Constants
constexpr float GRAVITY       = 0.3f;
constexpr float HEAT_DIFFUSE  = 0.02f;
constexpr float PRESSURE_EQ   = 0.15f;
constexpr float COOLING_RATE  = 5.0f;

// Fluid Constants
constexpr int PRESSURE_SCAN_DEPTH = 20;
constexpr float PRESSURE_WEIGHT   = 0.5f;
constexpr float GAS_DISPLACEMENT_CHANCE = 0.5f;

// Water Specific Constants
constexpr float WATER_MAX_FALL_SPEED = 5.0f;
constexpr float WATER_MAX_HORIZONTAL_SPEED = 3.0f;
constexpr float WATER_BOILING_POINT = 100.0f;
constexpr float WATER_FREEZING_POINT = 0.0f;
constexpr float WATER_DENSITY = 1.0f;
constexpr float WATER_VISCOSITY = 0.1f;
constexpr float WATER_SPREAD_FACTOR = 0.1f;
constexpr float WATER_FRICTION = 0.7f;
