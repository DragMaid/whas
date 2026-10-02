#include "whas/ui/map_thumbnails.h"
#include "whas/constants.h"
#include "whas/element/base/factory.h"
#include "whas/engine/renderer.h"
#include <array>
#include <filesystem>

namespace {

constexpr Color BACKDROP{15, 15, 20, 255}; // the world's backdrop in main

Color Over(Color top, Color under) {
  auto mix = [&](unsigned char a, unsigned char b) {
    return static_cast<unsigned char>((a * top.a + b * (255 - top.a)) / 255);
  };
  return {mix(top.r, under.r), mix(top.g, under.g), mix(top.b, under.b), 255};
}

} // namespace

MapThumbnails::~MapThumbnails() {
  for (auto &[id, texture] : m_cache)
    UnloadTexture(texture);
}

Image MapThumbnails::Render(const MapDef &map) {
  Image image = GenImageColor(WIDTH, HEIGHT, BACKDROP);
  if (map.cells.size() != static_cast<size_t>(GRID_W) * GRID_H)
    return image;
  // One fresh cell per element, so colours don't depend on any world
  SimulationConfig config = map.Config();
  Renderer renderer;
  std::array<Color, static_cast<size_t>(Element::COUNT)> colors{};
  for (size_t e = 0; e < colors.size(); ++e)
    colors[e] = Over(renderer.CellColor(ElementFactory::Create(
                         static_cast<Element>(e), config)),
                     BACKDROP);

  auto *pixels = static_cast<Color *>(image.data);
  for (int y = 0; y < HEIGHT; ++y)
    for (int x = 0; x < WIDTH; ++x) {
      int cx = x * GRID_W / WIDTH, cy = y * GRID_H / HEIGHT;
      uint8_t e = map.cells[static_cast<size_t>(cy) * GRID_W + cx];
      pixels[y * WIDTH + x] = colors[e < colors.size() ? e : 0];
    }
  // Where the players start
  for (int slot = 0; slot < 2; ++slot) {
    Vector2 s = map.spawns[slot];
    int x = static_cast<int>(s.x) * WIDTH / GRID_W;
    int y = static_cast<int>(s.y) * HEIGHT / GRID_H;
    ImageDrawRectangle(&image, x, y, 4, 6,
                       slot == 0 ? Color{230, 200, 120, 255}
                                 : Color{170, 190, 210, 255});
  }
  return image;
}

void MapThumbnails::Save(const MapDef &map) {
  Image image = Render(map);
  ExportImage(image, MapStore::ThumbnailPath(map.id).c_str());
  UnloadImage(image);
  Forget(map.id);
}

void MapThumbnails::Forget(const std::string &id) {
  if (auto it = m_cache.find(id); it != m_cache.end()) {
    UnloadTexture(it->second);
    m_cache.erase(it);
  }
}

const Texture2D &MapThumbnails::Get(const MapDef &map) {
  if (auto it = m_cache.find(map.id); it != m_cache.end())
    return it->second;
  std::string path = MapStore::ThumbnailPath(map.id);
  Image image = std::filesystem::exists(path) ? LoadImage(path.c_str())
                                              : Render(map);
  Texture2D texture = LoadTextureFromImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  UnloadImage(image);
  return m_cache.emplace(map.id, texture).first->second;
}
