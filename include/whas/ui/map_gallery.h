#pragma once
#include "whas/game/map.h"
#include "whas/ui/map_thumbnails.h"
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The saved maps as a wall of thumbnails: make, edit, copy, delete or play
// one. It also draws the map-pool picker that the solo and room setups use.
class MapGallery {
public:
  // A room's pool: up to three picks, each "" (unused), RANDOM or a map id
  static constexpr const char *RANDOM = "random";
  using Pool = std::array<std::string, MatchOptions::MAX_POOL>;

  MapGallery() { Reload(); }

  void Open() { m_open = true; }
  void Close() { m_open = false; }
  bool IsOpen() const { return m_open; }
  void Reload();

  // Inside the ImGui frame
  void Draw();
  // Three combo boxes; true when a pick changed
  bool DrawPoolPicker(Pool &pool);
  // The pool as match options (no picks at all: random arenas)
  MatchOptions BuildOptions(const Pool &pool) const;

  const std::vector<MapDef> &Maps() const { return m_maps; }
  const MapDef *Find(const std::string &id) const;
  MapThumbnails &Thumbnails() { return m_thumbnails; }

  // Requests for main: open the editor (empty = a new map), or play solo
  std::optional<std::optional<MapDef>> TakeEdit() {
    return std::exchange(m_edit, {});
  }
  std::optional<MapDef> TakePlay() { return std::exchange(m_play, {}); }

private:
  void DrawCard(const MapDef &map, float width);

  MapThumbnails m_thumbnails;
  std::vector<MapDef> m_maps;
  bool m_open = false;
  std::optional<std::optional<MapDef>> m_edit;
  std::optional<MapDef> m_play;
  std::string m_confirmDelete;
  std::string m_status;
};
