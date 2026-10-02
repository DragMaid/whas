#include "whas/ui/map_gallery.h"
#include "imgui.h"
#include "rlImGui.h"
#include "whas/engine/view.h"
#include "whas/ui/widgets.h"
#include <algorithm>

void MapGallery::Reload() { m_maps = MapStore().LoadAll(); }

const MapDef *MapGallery::Find(const std::string &id) const {
  for (const MapDef &m : m_maps)
    if (m.id == id)
      return &m;
  return nullptr;
}

void MapGallery::Draw() {
  if (!m_open)
    return;
  float scale = View::UiScale();
  ImGui::SetNextWindowPos({GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f},
                          ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({720.0f * scale, 520.0f * scale}, ImGuiCond_Appearing);
  if (!ImGui::Begin("Maps", &m_open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  if (Widgets::Button("New map")) {
    m_edit = std::optional<MapDef>{};
    m_open = false;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%d saved", static_cast<int>(m_maps.size()));
  if (!m_status.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", m_status.c_str());
  }
  ImGui::Separator();

  if (m_maps.empty())
    ImGui::TextDisabled("No maps yet. Make one, or rooms will use generated arenas.");

  float cardWidth = 200.0f * scale;
  float avail = ImGui::GetContentRegionAvail().x;
  int columns = std::max(1, static_cast<int>(avail / (cardWidth + 12.0f * scale)));
  ImGui::BeginChild("cards");
  for (size_t i = 0; i < m_maps.size(); ++i) {
    if (i % columns != 0)
      ImGui::SameLine();
    // Copy: a button may reload the list underneath us
    MapDef map = m_maps[i];
    DrawCard(map, cardWidth);
    if (i >= m_maps.size())
      break;
  }
  ImGui::EndChild();
  ImGui::End();
}

void MapGallery::DrawCard(const MapDef &map, float width) {
  ImGui::PushID(map.id.c_str());
  ImGui::BeginGroup();
  const Texture2D &thumb = m_thumbnails.Get(map);
  rlImGuiImageSize(&thumb, static_cast<int>(width),
                   static_cast<int>(width * MapThumbnails::HEIGHT / MapThumbnails::WIDTH));
  ImGui::TextUnformatted(map.name.c_str());
  int changes = 0;
  for (const auto &[group, fields] : map.settings.items())
    changes += static_cast<int>(fields.size());
  ImGui::SameLine();
  ImGui::TextDisabled("%s%s", ArenaGen::BiomeName(map.gen.biome),
                      changes ? " *" : "");
  if (changes && ImGui::IsItemHovered())
    ImGui::SetTooltip("%d world setting%s changed", changes, changes == 1 ? "" : "s");

  if (Widgets::SmallButton("Play")) {
    m_play = map;
    m_open = false;
  }
  ImGui::SameLine();
  if (Widgets::SmallButton("Edit")) {
    m_edit = std::optional<MapDef>{map};
    m_open = false;
  }
  ImGui::SameLine();
  if (Widgets::SmallButton("Copy")) {
    MapDef copy = map;
    copy.id = MapStore::NewId();
    copy.name = map.name.substr(0, 27) + " copy";
    std::string error;
    if (MapStore().Save(copy, error)) {
      m_thumbnails.Save(copy);
      Reload();
    } else {
      m_status = error;
    }
  }
  ImGui::SameLine();
  if (m_confirmDelete == map.id) {
    if (Widgets::SmallButton("Sure?")) {
      std::string error;
      if (!MapStore().Remove(map.id, error))
        m_status = error;
      m_thumbnails.Forget(map.id);
      m_confirmDelete.clear();
      Reload();
    }
  } else if (Widgets::SmallButton("Delete")) {
    m_confirmDelete = map.id;
  }
  ImGui::EndGroup();
  ImGui::PopID();
}

bool MapGallery::DrawPoolPicker(Pool &pool) {
  bool changed = false;
  auto label = [&](const std::string &pick) -> std::string {
    if (pick.empty())
      return "-";
    if (pick == RANDOM)
      return "Random arena";
    const MapDef *m = Find(pick);
    return m ? m->name : "(missing map)";
  };
  for (int i = 0; i < MatchOptions::MAX_POOL; ++i) {
    ImGui::PushID(i);
    ImGui::SetNextItemWidth(200.0f * View::UiScale());
    char title[16];
    std::snprintf(title, sizeof title, "Map %d", i + 1);
    if (ImGui::BeginCombo(title, label(pool[i]).c_str())) {
      auto option = [&](const std::string &value) {
        if (ImGui::Selectable(label(value).c_str(), pool[i] == value)) {
          pool[i] = value;
          changed = true;
        }
      };
      if (i > 0)
        option("");
      option(RANDOM);
      for (const MapDef &m : m_maps)
        option(m.id);
      ImGui::EndCombo();
    }
    if (!pool[i].empty() && pool[i] != RANDOM)
      if (const MapDef *m = Find(pool[i])) {
        ImGui::SameLine();
        const Texture2D &thumb = m_thumbnails.Get(*m);
        rlImGuiImageSize(&thumb, static_cast<int>(48 * View::UiScale()),
                         static_cast<int>(27 * View::UiScale()));
      }
    ImGui::PopID();
  }
  ImGui::TextDisabled("Rounds take turns through these maps.");
  return changed;
}

MatchOptions MapGallery::BuildOptions(const Pool &pool) const {
  MatchOptions options;
  for (const std::string &pick : pool) {
    if (pick.empty())
      continue;
    MapSpec spec;
    if (pick != RANDOM)
      if (const MapDef *m = Find(pick))
        spec.custom = *m;
    options.pool.push_back(std::move(spec));
  }
  return options;
}
