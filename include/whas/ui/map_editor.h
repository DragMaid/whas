#pragma once
#include "whas/game/map.h"
#include <optional>
#include <string>
#include <utility>

class Simulation;
class MapThumbnails;

// Builds a map in the live world: generate an arena from a biome, seed and
// knobs, paint over it (left button paints, right erases), drag the two
// spawns, and pick the world settings it plays by. The world stays frozen
// unless "Let it settle" runs it, so water can find its level before saving.
class MapEditor {
public:
  explicit MapEditor(MapThumbnails &thumbnails) : m_thumbnails(thumbnails) {}

  // Edit a saved map, or start a new one from a generated arena
  void Open(Simulation &sim, std::optional<MapDef> map = std::nullopt);
  void Close() { m_open = false; }
  bool IsOpen() const { return m_open; }

  // Mouse on the world and, while settling, the simulation
  void Update(Simulation &sim);
  // Inside the world camera: spawns and the brush
  void DrawWorld() const;
  // Inside the ImGui frame
  void DrawPanel(Simulation &sim);

  // A map was saved (the gallery reloads)
  bool TakeSaved() { return std::exchange(m_saved, false); }
  // "Play it": a solo match on this map
  std::optional<MapDef> TakeTest() { return std::exchange(m_test, {}); }

private:
  void Generate(Simulation &sim);
  void ApplySettings(Simulation &sim);
  bool Save(Simulation &sim, bool asCopy);
  void DrawGenerate(Simulation &sim);
  void DrawBrush();
  void DrawSettings(Simulation &sim);
  int SpawnAt(Vector2 cell) const;

  MapThumbnails &m_thumbnails;
  bool m_open = false;
  MapDef m_map;
  SimulationConfig m_config;
  char m_name[33]{};
  bool m_running = false; // let the world settle
  bool m_panelHidden = false;
  float m_accumulator = 0.0f;
  Element m_brushElement = Element::EARTH;
  int m_brush = 3;
  int m_dragging = -1; // spawn being dragged
  Vector2 m_dragOffset{};
  bool m_saved = false;
  std::optional<MapDef> m_test;
  std::string m_status;
};
