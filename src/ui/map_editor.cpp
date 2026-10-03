#include "whas/ui/map_editor.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/core/config_json.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/character.h"
#include "whas/game/turn_controller.h"
#include "whas/ui/editor_icons.h"
#include "whas/ui/map_thumbnails.h"
#include "whas/ui/theme.h"
#include "whas/ui/widgets.h"
#include <algorithm>
#include <cstring>
#include <random>

namespace {

constexpr Color kSpawnColors[2] = {{230, 200, 120, 255}, {170, 190, 210, 255}};

uint64_t FreshSeed() {
  static std::mt19937_64 rng{std::random_device{}()};
  return rng() >> 1;
}

bool Solid(const Simulation &sim, int x, int y) {
  if (x < 0 || x >= GRID_W || y < 0)
    return false;
  if (y >= GRID_H)
    return true;
  Element e = sim.GetCell(x, y).element;
  return e != Element::AIR && e != Element::WATER && e != Element::STEAM &&
         e != Element::CLOUD && e != Element::SMOKE && e != Element::FIRE;
}

// Drop a spawn onto the ground under it, or lift it out of the ground
Vector2 Settle(const Simulation &sim, Vector2 spawn) {
  int x = std::clamp(static_cast<int>(spawn.x), 0,
                     GRID_W - static_cast<int>(Character::WIDTH));
  int y = std::clamp(static_cast<int>(spawn.y), 0,
                     GRID_H - static_cast<int>(Character::HEIGHT));
  auto rowSolid = [&](int row) {
    for (int cx = x; cx < x + static_cast<int>(Character::WIDTH); ++cx)
      if (Solid(sim, cx, row))
        return true;
    return false;
  };
  auto bodyBlocked = [&](int top) {
    for (int row = top; row < top + static_cast<int>(Character::HEIGHT); ++row)
      if (rowSolid(row))
        return true;
    return false;
  };
  while (y > 0 && bodyBlocked(y))
    --y;
  while (y + static_cast<int>(Character::HEIGHT) < GRID_H &&
         !rowSolid(y + static_cast<int>(Character::HEIGHT)))
    ++y;
  return {static_cast<float>(x), static_cast<float>(y)};
}

} // namespace

void MapEditor::Open(Simulation &sim, std::optional<MapDef> map) {
  m_open = true;
  m_running = false;
  m_dragging = -1;
  m_status.clear();
  m_brush.ClearHistory();
  if (map) {
    m_map = std::move(*map);
    m_config = m_map.Config();
    Maps::Build(sim, m_map, m_map.genSeed);
  } else {
    m_map = {};
    m_map.name = "New map";
    m_map.genSeed = FreshSeed();
    m_config = SimulationConfig{};
    Generate(sim);
  }
  std::snprintf(m_name, sizeof m_name, "%s", m_map.name.c_str());
}

void MapEditor::Generate(Simulation &sim) {
  sim.GetConfig() = m_config;
  sim.Restart(m_map.genSeed);
  m_map.spawns = ArenaGen::Generate(sim, m_map.genSeed, m_map.gen).spawns;
  m_brush.ClearHistory();
}

void MapEditor::ApplySettings(Simulation &sim) {
  sim.GetConfig() = m_config;
  m_map.settings = ConfigDiff(m_config);
}

int MapEditor::SpawnAt(Vector2 cell) const {
  for (int i = 0; i < 2; ++i) {
    Vector2 s = m_map.spawns[i];
    if (cell.x >= s.x && cell.x < s.x + Character::WIDTH && cell.y >= s.y &&
        cell.y < s.y + Character::HEIGHT)
      return i;
  }
  return -1;
}

void MapEditor::Update(Simulation &sim) {
  if (!m_open)
    return;
  Vector2 cell = View::MouseCells();
  bool overUi = ImGui::GetIO().WantCaptureMouse;

  if (m_dragging >= 0) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      m_map.spawns[m_dragging] = {cell.x - m_dragOffset.x,
                                  cell.y - m_dragOffset.y};
    } else {
      m_map.spawns[m_dragging] = Settle(sim, m_map.spawns[m_dragging]);
      m_dragging = -1;
    }
  } else {
    if (!ImGui::GetIO().WantCaptureKeyboard)
      m_brush.HandleKeys(sim);
    if (!overUi && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      int spawn = SpawnAt(cell);
      if (spawn >= 0) {
        m_dragging = spawn;
        m_dragOffset = {cell.x - m_map.spawns[spawn].x,
                        cell.y - m_map.spawns[spawn].y};
      }
    }
    if (m_dragging < 0)
      m_brush.Paint(sim, cell, overUi);
  }

  if (m_running) {
    m_accumulator += std::min(GetFrameTime(), 0.1f);
    while (m_accumulator >= TurnController::TICK_DT) {
      sim.Update(TurnController::TICK_DT,
                 IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !overUi);
      m_accumulator -= TurnController::TICK_DT;
    }
  }
}

void MapEditor::DrawWorld() const {
  if (!m_open)
    return;
  for (int i = 0; i < 2; ++i) {
    Vector2 s = m_map.spawns[i];
    Rectangle r{s.x * CELL_SIZE, s.y * CELL_SIZE, Character::WIDTH * CELL_SIZE,
                Character::HEIGHT * CELL_SIZE};
    Color c = kSpawnColors[i];
    DrawRectangleRec(r, Fade(c, 0.25f));
    DrawRectangleLinesEx(r, 2.0f, c);
    DrawText(i == 0 ? "I" : "II", static_cast<int>(r.x + r.width / 2 - 4),
             static_cast<int>(r.y - 22), 20, c);
  }
  if (m_dragging < 0 && !ImGui::GetIO().WantCaptureMouse)
    m_brush.DrawCursor(View::MouseCells());
}

bool MapEditor::Save(Simulation &sim, bool asCopy) {
  m_map.name = m_name;
  if (m_map.name.empty()) {
    m_status = "Give the map a name first.";
    return false;
  }
  if (asCopy || m_map.id.empty())
    m_map.id = MapStore::NewId();
  m_map.cells = Maps::CaptureCells(sim);
  m_map.settings = ConfigDiff(m_config);
  std::string error;
  if (!MapStore().Save(m_map, error)) {
    m_status = error;
    return false;
  }
  m_thumbnails.Save(m_map);
  m_saved = true;
  m_status = "Saved \"" + m_map.name + "\"";
  return true;
}

void MapEditor::DrawPanel(Simulation &sim) {
  if (!m_open)
    return;
  if (IsKeyPressed(KEY_TAB) && !ImGui::GetIO().WantTextInput)
    m_panelHidden = !m_panelHidden;
  if (m_panelHidden) {
    ImGui::SetNextWindowPos({GetScreenWidth() - 12.0f * View::UiScale(),
                             12.0f * View::UiScale()},
                            ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::Begin("##mapEditorTab", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
    if (Widgets::Button("Show map editor (Tab)"))
      m_panelHidden = false;
    ImGui::End();
    return;
  }
  float scale = View::UiScale();
  DrawToolbar(sim, 340.0f * scale);
  ImGui::SetNextWindowPos({GetScreenWidth() - 12.0f * scale, 12.0f * scale},
                          ImGuiCond_Always, {1.0f, 0.0f});
  ImGui::SetNextWindowSize({340.0f * scale, GetScreenHeight() - 24.0f * scale},
                           ImGuiCond_Always);
  ImGui::Begin("Map editor", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize);

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##name", "Map name", m_name, sizeof m_name);
  if (Widgets::Button("Save"))
    Save(sim, false);
  ImGui::SameLine();
  if (Widgets::Button("Save as copy"))
    Save(sim, true);
  ImGui::SameLine();
  if (Widgets::Button("Play it")) {
    m_map.name = m_name[0] ? m_name : "Untitled";
    m_map.cells = Maps::CaptureCells(sim);
    m_map.settings = ConfigDiff(m_config);
    m_test = m_map;
    m_open = false;
  }
  ImGui::SameLine();
  if (Widgets::Button("Close"))
    m_open = false;
  if (!m_status.empty())
    ImGui::TextWrapped("%s", m_status.c_str());

  if (ImGui::BeginTabBar("tabs")) {
    if (ImGui::BeginTabItem("Generate")) {
      DrawGenerate(sim);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("World settings")) {
      DrawSettings(sim);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void MapEditor::DrawGenerate(Simulation &sim) {
  int biome = static_cast<int>(m_map.gen.biome);
  const char *names[static_cast<int>(ArenaGen::Biome::COUNT)];
  for (int i = 0; i < static_cast<int>(ArenaGen::Biome::COUNT); ++i)
    names[i] = ArenaGen::BiomeName(static_cast<ArenaGen::Biome>(i));
  if (ImGui::Combo("Biome", &biome, names, IM_ARRAYSIZE(names)))
    m_map.gen.biome = static_cast<ArenaGen::Biome>(biome);

  char seed[24];
  std::snprintf(seed, sizeof seed, "%llu",
                static_cast<unsigned long long>(m_map.genSeed));
  ImGui::SetNextItemWidth(160.0f * View::UiScale());
  if (ImGui::InputText("##seed", seed, sizeof seed,
                       ImGuiInputTextFlags_CharsDecimal))
    m_map.genSeed = std::strtoull(seed, nullptr, 10);
  ImGui::SameLine();
  if (Widgets::Button("New seed"))
    m_map.genSeed = FreshSeed();

  ImGui::SliderInt("Hills %", &m_map.gen.hills, 0, 300);
  ImGui::SliderInt("Water rise", &m_map.gen.waterRise, -40, 80);
  ImGui::SliderInt("Plants %", &m_map.gen.vegetation, 0, 400);
  ImGui::SliderInt("Rocks %", &m_map.gen.rocks, 0, 400);
  if (Widgets::Button("Generate (replaces the painting)", {-1, 0}))
    Generate(sim);
  if (Widgets::Button("Clear to bare rock", {-1, 0})) {
    sim.GetConfig() = m_config;
    sim.Restart(m_map.genSeed);
    for (int y = ArenaGen::FLOOR_BOTTOM - 3; y < GRID_H; ++y)
      for (int x = 0; x < GRID_W; ++x)
        sim.Paint(x, y, Element::ROCK, 0);
    for (int i = 0; i < 2; ++i)
      m_map.spawns[i] = Settle(sim, m_map.spawns[i]);
    m_brush.ClearHistory();
  }
}

// Paint palette and world controls along the top
void MapEditor::DrawToolbar(Simulation &sim, float panelWidth) {
  float s = View::UiScale();
  ImGui::SetNextWindowPos({8 * s, 8 * s}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({GetScreenWidth() - panelWidth - 24 * s, 0}, ImGuiCond_Always);
  ImGui::Begin("##mapToolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_AlwaysAutoResize);
  if (IconButton("##settle", EditorIcon::Settle, m_running,
                 m_running ? "Freeze the world"
                           : "Let it settle: run the world so water and sand come to rest"))
    m_running = !m_running;
  ImGui::SameLine(0, 12 * s);
  m_brush.DrawPalette();
  ImGui::SameLine();
  WrapToolbar(60 * s);
  ImGui::BeginDisabled(!m_brush.CanUndo());
  if (Widgets::SmallButton("Undo"))
    m_brush.Undo(sim);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Ctrl+Z");
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Theme::Tone::Faint));
  ImGui::TextWrapped("Left paints, right erases, wheel or [ ] sizes, 1-9 pick, "
                     "E eraser. Drag I and II to move the spawns. Tab hides the panel.");
  ImGui::PopStyleColor();
  ImGui::End();
}

void MapEditor::DrawSettings(Simulation &sim) {
  bool changed = false;
  // The ones people reach for
  changed |= ImGui::Checkbox("Endless rain from clouds", &m_config.cloud.infiniteRain);
  changed |= ImGui::SliderFloat("Sky rain", &m_config.cloud.skyRain, 0.0f, 8.0f, "%.1f drops/tick");
  changed |= ImGui::SliderFloat("Gravity", &m_config.world.gravity, -1.0f, 2.0f);
  changed |= ImGui::SliderFloat("Temperature", &m_config.world.ambientTemp, -50.0f, 100.0f, "%.0f C");

  ImGui::TextDisabled("Presets:");
  struct Preset {
    const char *name;
    void (*apply)(SimulationConfig &);
  };
  static const Preset presets[] = {
      {"Defaults", [](SimulationConfig &c) { c = SimulationConfig{}; }},
      {"Endless storm",
       [](SimulationConfig &c) {
         c.cloud.infiniteRain = true;
         c.cloud.skyRain = 2.0f;
       }},
      {"Drizzle", [](SimulationConfig &c) { c.cloud.skyRain = 0.3f; }},
      {"Moon gravity", [](SimulationConfig &c) { c.world.gravity = 0.35f; }},
      {"Deep winter", [](SimulationConfig &c) { c.world.ambientTemp = -25.0f; }},
      {"Scorched", [](SimulationConfig &c) { c.world.ambientTemp = 60.0f; }},
  };
  int n = 0;
  for (const Preset &p : presets) {
    if (Widgets::SmallButton(p.name)) {
      p.apply(m_config);
      changed = true;
    }
    if (++n % 3 != 0)
      ImGui::SameLine();
  }
  ImGui::NewLine();

  if (ImGui::TreeNode("All settings")) {
    // Fields come grouped; one tree node per group, elements under one more
    std::string open; // group whose node is open, "" for a closed one
    bool inGroup = false, elementsOpen = false, inElements = false;
    std::string current;
    ForEachConfigField(m_config, [&](const ConfigField &f, void *v) {
      if (current != f.group) {
        if (inGroup && !open.empty())
          ImGui::TreePop();
        current = f.group;
        inGroup = true;
        bool isElement = std::strncmp(f.group, "Element/", 8) == 0;
        if (isElement && !inElements) {
          inElements = true;
          elementsOpen = ImGui::TreeNode("Elements");
        }
        if (isElement && !elementsOpen) {
          open.clear();
          return;
        }
        open = ImGui::TreeNode(isElement ? f.group + 8 : f.group) ? f.group : "";
      }
      if (open.empty())
        return;
      ImGui::PushID(f.key);
      switch (f.kind) {
      case ConfigField::Kind::Float:
        changed |= ImGui::SliderFloat(f.label, static_cast<float *>(v), f.min, f.max);
        break;
      case ConfigField::Kind::Int:
        changed |= ImGui::SliderInt(f.label, static_cast<int *>(v),
                                    static_cast<int>(f.min), static_cast<int>(f.max));
        break;
      case ConfigField::Kind::Bool:
        changed |= ImGui::Checkbox(f.label, static_cast<bool *>(v));
        break;
      }
      ImGui::PopID();
    });
    if (inGroup && !open.empty())
      ImGui::TreePop();
    if (elementsOpen)
      ImGui::TreePop();
    ImGui::TreePop();
  }

  int changes = 0;
  for (const auto &[group, fields] : ConfigDiff(m_config).items())
    changes += static_cast<int>(fields.size());
  ImGui::TextDisabled("%d setting%s differ from the defaults", changes,
                      changes == 1 ? "" : "s");
  if (changed)
    ApplySettings(sim);
}
