#pragma once
#include "whas/game/map.h"
#include <raylib.h>
#include <string>
#include <unordered_map>

// Small pictures of maps for the gallery and the room setup: one pixel per
// two cells, coloured like the world. Saved next to the map as a PNG so the
// gallery opens without building every map.
class MapThumbnails {
public:
  static constexpr int WIDTH = 160;
  static constexpr int HEIGHT = 90;

  MapThumbnails() = default;
  ~MapThumbnails();
  MapThumbnails(const MapThumbnails &) = delete;
  MapThumbnails &operator=(const MapThumbnails &) = delete;

  static Image Render(const MapDef &map);
  // Render and write the PNG, and drop any cached texture
  void Save(const MapDef &map);
  void Forget(const std::string &id);
  // Loaded from the PNG, or rendered when it is missing
  const Texture2D &Get(const MapDef &map);

private:
  std::unordered_map<std::string, Texture2D> m_cache;
};
