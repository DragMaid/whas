#include "whas/campaign/campaign_editor.h"
#include "imgui.h"
#include "whas/campaign/campaign_play.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/character.h"
#include "whas/game/character_draw.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/glyph_docs.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include "whas/ui/widgets.h"
#include <algorithm>
#include <filesystem>

using namespace Campaign;
using Theme::Tone;

namespace {

constexpr Element kBrushElements[] = {
    Element::EARTH, Element::ROCK,  Element::SAND,  Element::GRASS,
    Element::WOOD,  Element::WATER, Element::ICE,   Element::CLOUD,
    Element::FIRE,  Element::AIR};

bool Solid(const Simulation &sim, int x, int y) {
  if (y >= GRID_H)
    return true;
  if (x < 0 || x >= GRID_W || y < 0)
    return false;
  const Cell &c = sim.GetCell(x, y);
  if (c.element == Element::AIR)
    return false;
  const auto &props = sim.GetConfig().elements[static_cast<size_t>(c.element)];
  return props.solid && !props.passable;
}

// Objects stand on the floor: the first solid cell at or below the point
Vector2 OnFloor(const Simulation &sim, Vector2 cell) {
  int x = std::clamp(static_cast<int>(cell.x), 0, GRID_W - 1);
  int y = std::clamp(static_cast<int>(cell.y), 0, GRID_H - 1);
  while (y < GRID_H && !Solid(sim, x, y))
    ++y;
  while (y > 0 && Solid(sim, x, y - 1))
    --y;
  return {x + 0.5f, static_cast<float>(y)};
}

Rectangle ObjectBox(const ObjectDef &o) {
  return {o.pos.x - 6.0f, o.pos.y - 19.0f, 12.0f, 19.0f};
}

Rectangle BodyBox(Vector2 pos) {
  return {pos.x, pos.y, Character::WIDTH, Character::HEIGHT};
}

Color EnemyTint(EnemyKind kind) {
  switch (kind) {
  case EnemyKind::Mage:
    return {170, 110, 230, 255};
  case EnemyKind::Undead:
    return {120, 190, 110, 255};
  default:
    return {230, 170, 80, 255};
  }
}

} // namespace

void CampaignEditor::OpenHub() {
  m_hub = true;
  m_confirm.clear();
  Reload();
}

void CampaignEditor::Reload() { m_campaigns = LoadAll(); }

void CampaignEditor::Open(Simulation &sim, const CampaignDef &def) {
  m_def = def;
  m_editing = true;
  m_hub = false;
  m_tool = Tool::Paint;
  m_selected = {};
  m_running = false;
  m_status.clear();
  std::snprintf(m_name, sizeof m_name, "%s", def.name.c_str());
  LoadRoomIntoWorld(sim, def.startRoom);
}

void CampaignEditor::LoadRoomIntoWorld(Simulation &sim, RoomPos pos) {
  std::string error;
  auto room = LoadRoom(m_def.id, pos, error);
  m_room = room ? std::move(*room) : BlankRoom(pos);
  Maps::Build(sim, m_room.terrain, 1);
  m_selected = {};
  SetBackground(m_room.background);
}

void CampaignEditor::SetBackground(const std::string &file) {
  m_room.background = file;
  if (m_background.id)
    UnloadTexture(m_background);
  m_background = {};
  if (!file.empty())
    m_background = LoadTexture(BackgroundPath(m_def.id, file).c_str());
}

bool CampaignEditor::StoreRoom(Simulation &sim) {
  m_room.terrain.cells = Maps::CaptureCells(sim);
  m_def.name = m_name[0] ? m_name : "Untitled";
  std::string error;
  if (!SaveRoom(m_def.id, m_room, error) || !SaveDef(m_def, error)) {
    m_status = "Not saved: " + error;
    return false;
  }
  m_status = "Saved.";
  return true;
}

void CampaignEditor::AddRoom(Simulation &sim, int dx, int dy) {
  if (!StoreRoom(sim))
    return;
  RoomPos pos = m_room.pos.Step(dx, dy);
  m_def.rooms.push_back(pos);
  std::string error;
  RoomDef room = BlankRoom(pos);
  // The new room plays by the same world settings as the one it grew from
  room.terrain.settings = m_room.terrain.settings;
  if (!SaveRoom(m_def.id, room, error) || !SaveDef(m_def, error)) {
    m_status = "Not added: " + error;
    m_def.rooms.pop_back();
    return;
  }
  LoadRoomIntoWorld(sim, pos);
}

void CampaignEditor::DeleteRoom(Simulation &sim) {
  if (m_room.pos == m_def.startRoom) {
    m_status = "The starting room stays; move the start first.";
    return;
  }
  std::string error;
  RemoveRoom(m_def.id, m_room.pos, error);
  std::erase(m_def.rooms, m_room.pos);
  SaveDef(m_def, error);
  LoadRoomIntoWorld(sim, m_def.startRoom);
}

void CampaignEditor::Close(Simulation &sim) {
  if (!m_editing)
    return;
  StoreRoom(sim);
  m_editing = false;
  if (m_background.id)
    UnloadTexture(m_background);
  m_background = {};
  OpenHub();
}

void CampaignEditor::Resume(Simulation &sim) {
  Maps::Build(sim, m_room.terrain, 1);
}

CampaignEditor::Selection CampaignEditor::HitTest(Vector2 cell) const {
  for (int i = static_cast<int>(m_room.enemies.size()) - 1; i >= 0; --i)
    if (CheckCollisionPointRec(cell, BodyBox(m_room.enemies[i].pos)))
      return {Selection::Enemy, i};
  for (int i = static_cast<int>(m_room.objects.size()) - 1; i >= 0; --i)
    if (CheckCollisionPointRec(cell, ObjectBox(m_room.objects[i])))
      return {Selection::Object, i};
  if (m_room.pos == m_def.startRoom &&
      CheckCollisionPointRec(cell, BodyBox(m_def.startPos)))
    return {Selection::Start, 0};
  return {};
}

void CampaignEditor::Place(const Simulation &sim, Vector2 cell) {
  Vector2 body{cell.x - Character::WIDTH * 0.5f, cell.y - Character::HEIGHT * 0.5f};
  auto object = [&](ObjectKind kind) {
    ObjectDef o;
    o.kind = kind;
    o.pos = OnFloor(sim, cell);
    if (kind == ObjectKind::Shrine) {
      o.glyph = "water";
      o.sigil = true;
    }
    m_room.objects.push_back(o);
    m_selected = {Selection::Object, static_cast<int>(m_room.objects.size()) - 1};
  };
  auto enemy = [&](EnemyKind kind) {
    EnemyDef e;
    e.kind = kind;
    e.pos = body;
    if (kind == EnemyKind::Mage) {
      e.hp = 30.0f;
      e.speed = 18.0f;
      e.damage = 4.0f;
    } else if (kind == EnemyKind::Flyer) {
      e.hp = 20.0f;
      e.speed = 26.0f;
      e.damage = 6.0f;
    }
    m_room.enemies.push_back(e);
    m_selected = {Selection::Enemy, static_cast<int>(m_room.enemies.size()) - 1};
  };
  switch (m_tool) {
  case Tool::Gate:
    object(ObjectKind::Gate);
    break;
  case Tool::Workbench:
    object(ObjectKind::Workbench);
    break;
  case Tool::Shrine:
    object(ObjectKind::Shrine);
    break;
  case Tool::Start:
    m_def.startRoom = m_room.pos;
    m_def.startPos = body;
    m_selected = {Selection::Start, 0};
    break;
  case Tool::Mage:
    enemy(EnemyKind::Mage);
    break;
  case Tool::Undead:
    enemy(EnemyKind::Undead);
    break;
  case Tool::Flyer:
    enemy(EnemyKind::Flyer);
    break;
  default:
    break;
  }
}

void CampaignEditor::RemoveSelected() {
  if (m_selected.kind == Selection::Object)
    m_room.objects.erase(m_room.objects.begin() + m_selected.index);
  else if (m_selected.kind == Selection::Enemy)
    m_room.enemies.erase(m_room.enemies.begin() + m_selected.index);
  m_selected = {};
}

void CampaignEditor::Update(Simulation &sim) {
  if (!m_editing)
    return;
  // Drop a PNG on the window: this room's background
  if (IsFileDropped()) {
    FilePathList files = LoadDroppedFiles();
    if (files.count > 0) {
      std::string error;
      if (auto file = ImportBackground(m_def.id, files.paths[0], error)) {
        SetBackground(*file);
        m_status = "Background set: " + *file;
      } else {
        m_status = error;
      }
    }
    UnloadDroppedFiles(files);
  }

  bool keys = !ImGui::GetIO().WantCaptureKeyboard;
  if (keys && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S))
    StoreRoom(sim);
  if (keys && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)))
    RemoveSelected();

  if (m_running) {
    m_accumulator += std::min(GetFrameTime(), 0.1f);
    while (m_accumulator >= TurnController::TICK_DT) {
      m_accumulator -= TurnController::TICK_DT;
      sim.Update(TurnController::TICK_DT);
    }
  }

  Vector2 cell = View::MouseCells();
  if (m_dragging) {
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      m_dragging = false;
      if (m_selected.kind == Selection::Object)
        m_room.objects[m_selected.index].pos =
            OnFloor(sim, m_room.objects[m_selected.index].pos);
      return;
    }
    Vector2 to{cell.x - m_dragOffset.x, cell.y - m_dragOffset.y};
    if (m_selected.kind == Selection::Object)
      m_room.objects[m_selected.index].pos = to;
    else if (m_selected.kind == Selection::Enemy)
      m_room.enemies[m_selected.index].pos = to;
    else if (m_selected.kind == Selection::Start)
      m_def.startPos = to;
    return;
  }
  if (ImGui::GetIO().WantCaptureMouse)
    return;

  if (m_tool == Tool::Paint) {
    int cx = static_cast<int>(cell.x), cy = static_cast<int>(cell.y);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
      sim.Paint(cx, cy, m_brushElement, m_brush);
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
      sim.Erase(cx, cy, m_brush);
    float wheel = GetMouseWheelMove();
    if (wheel != 0)
      m_brush = std::clamp(m_brush + static_cast<int>(wheel), 0, 20);
    return;
  }
  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    m_selected = {};
    return;
  }
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    return;
  // Anything already there is picked up and dragged, whatever the tool
  Selection hit = HitTest(cell);
  if (hit.kind == Selection::None && m_tool != Tool::Select) {
    Place(sim, cell);
    return;
  }
  m_selected = hit;
  if (hit.kind == Selection::None)
    return;
  Vector2 at = hit.kind == Selection::Object ? m_room.objects[hit.index].pos
               : hit.kind == Selection::Enemy ? m_room.enemies[hit.index].pos
                                              : m_def.startPos;
  m_dragOffset = {cell.x - at.x, cell.y - at.y};
  m_dragging = true;
}

void CampaignEditor::DrawBackground() const {
  if (!m_editing || !m_background.id)
    return;
  DrawTexturePro(m_background,
                 {0, 0, (float)m_background.width, (float)m_background.height},
                 {0, 0, (float)(GRID_W * CELL_SIZE), (float)(GRID_H * CELL_SIZE)},
                 {0, 0}, 0.0f, WHITE);
}

void CampaignEditor::DrawWorld(const UI &ui) const {
  if (!m_editing)
    return;
  auto px = [](Rectangle r) {
    return Rectangle{r.x * CELL_SIZE, r.y * CELL_SIZE, r.width * CELL_SIZE,
                     r.height * CELL_SIZE};
  };
  Color brass = Theme::Rl(Tone::BrassBright, 0.95f);
  for (int i = 0; i < static_cast<int>(m_room.objects.size()); ++i) {
    DrawCampaignObject(m_room.objects[i], true, ui.Glyphs());
    if (m_selected.kind == Selection::Object && m_selected.index == i)
      DrawRectangleLinesEx(px(ObjectBox(m_room.objects[i])), 2, brass);
  }
  for (int i = 0; i < static_cast<int>(m_room.enemies.size()); ++i) {
    const EnemyDef &e = m_room.enemies[i];
    Character body;
    body.pos = e.pos;
    body.grounded = true;
    DrawCharacterBody(body, EnemyTint(e.kind), false);
    Rectangle r = px(BodyBox(e.pos));
    Theme::DrawText(Theme::RlBody(), EnemyName(e.kind), {r.x - 8, r.y - 40}, 16,
                    EnemyTint(e.kind));
    if (m_selected.kind == Selection::Enemy && m_selected.index == i)
      DrawRectangleLinesEx(r, 2, brass);
  }
  if (m_room.pos == m_def.startRoom) {
    Rectangle r = px(BodyBox(m_def.startPos));
    DrawRectangleLinesEx(r, 2, Color{120, 220, 160, 255});
    Theme::DrawText(Theme::RlBody(), "START", {r.x - 10, r.y - 22}, 16,
                    Color{120, 220, 160, 255});
  }
  if (m_tool == Tool::Paint && !ImGui::GetIO().WantCaptureMouse) {
    Vector2 m = View::MouseCells();
    DrawCircleLinesV({m.x * CELL_SIZE, m.y * CELL_SIZE},
                     (m_brush + 0.5f) * CELL_SIZE, Theme::Rl(Tone::Parchment, 0.6f));
  }
}

void CampaignEditor::DrawPanel(Simulation &sim, UI &ui) {
  if (m_hub && !m_editing)
    DrawHub();
  if (auto def = std::exchange(m_editRequest, {}))
    Open(sim, *def);
  if (!m_editing)
    return;

  float scale = View::UiScale();
  ImGui::SetNextWindowPos({GetScreenWidth() - 12.0f * scale, 12.0f * scale},
                          ImGuiCond_Always, {1.0f, 0.0f});
  ImGui::SetNextWindowSize({360.0f * scale, GetScreenHeight() - 24.0f * scale},
                           ImGuiCond_Always);
  ImGui::Begin("Campaign editor", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##name", "Campaign name", m_name, sizeof m_name);
  if (Widgets::Button("Save"))
    StoreRoom(sim);
  ImGui::SameLine();
  if (Widgets::Button("Play this room")) {
    if (StoreRoom(sim)) {
      Vector2 at = m_room.pos == m_def.startRoom
                       ? m_def.startPos
                       : Vector2{GRID_W * 0.5f, GRID_H * 0.5f};
      m_test = TestRequest{m_def, m_room.pos, at};
    }
  }
  ImGui::SameLine();
  if (Widgets::Button("Close")) {
    Close(sim);
    ImGui::End();
    return;
  }
  if (!m_status.empty())
    ImGui::TextWrapped("%s", m_status.c_str());

  if (ImGui::CollapsingHeader("Rooms", ImGuiTreeNodeFlags_DefaultOpen))
    DrawRoomMap(sim);
  if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen))
    DrawTools();
  if (m_tool == Tool::Paint) {
    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
      DrawPaint(sim);
  } else if (ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSelected(ui);
  }
  if (ImGui::CollapsingHeader("Room"))
    DrawRoomSettings();
  if (ImGui::CollapsingHeader("Starting kit"))
    DrawStartingKit(ui);
  ImGui::End();
}

void CampaignEditor::DrawHub() {
  float scale = View::UiScale();
  ImGui::SetNextWindowPos({GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f},
                          ImGuiCond_Appearing, {0.5f, 0.5f});
  ImGui::SetNextWindowSize({620 * scale, 460 * scale}, ImGuiCond_Appearing);
  if (!ImGui::Begin("Campaigns", &m_hub, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  Widgets::Title("Campaigns");
  ImGui::TextDisabled("Rooms to explore, a sigil at a time. Edit one to build it.");
  ImGui::BeginChild("list", {0, -60 * scale}, true);
  for (const CampaignDef &c : m_campaigns) {
    ImGui::PushID(c.id.c_str());
    bool hasSave = LoadSave(c.id).has_value();
    ImGui::Text("%s", c.name.c_str());
    ImGui::SameLine(260 * scale);
    ImGui::TextDisabled("%d room%s", (int)c.rooms.size(), c.rooms.size() == 1 ? "" : "s");
    ImGui::SameLine(340 * scale);
    if (hasSave && Widgets::SmallButton("Continue"))
      m_play = PlayRequest{c, false};
    if (hasSave)
      ImGui::SameLine();
    bool confirmNew = m_confirm == c.id && !m_confirmDelete;
    if (Widgets::SmallButton(confirmNew ? "Wipe save?" : "New game")) {
      if (!hasSave || confirmNew) {
        m_play = PlayRequest{c, true};
        m_confirm.clear();
      } else {
        m_confirm = c.id;
        m_confirmDelete = false;
      }
    }
    ImGui::SameLine();
    if (Widgets::SmallButton("Edit"))
      m_editRequest = c;
    ImGui::SameLine();
    bool confirmDel = m_confirm == c.id && m_confirmDelete;
    if (Widgets::SmallButton(confirmDel ? "Delete?" : "Delete")) {
      if (confirmDel) {
        std::string error;
        Remove(c.id, error);
        m_confirm.clear();
        ImGui::PopID();
        Reload();
        break;
      }
      m_confirm = c.id;
      m_confirmDelete = true;
    }
    ImGui::PopID();
  }
  if (m_campaigns.empty())
    ImGui::TextDisabled("None yet: name one below.");
  ImGui::EndChild();
  ImGui::SetNextItemWidth(300 * scale);
  ImGui::InputTextWithHint("##new", "New campaign name", m_newName, sizeof m_newName);
  ImGui::SameLine();
  if (Widgets::Button("Create") && m_newName[0]) {
    CampaignDef c;
    c.name = m_newName;
    c.id = NewId(c.name);
    c.rooms = {{0, 0}};
    c.startPos = {GRID_W * 0.5f, GRID_H - 12.0f - Character::HEIGHT};
    std::string error;
    if (SaveDef(c, error) && SaveRoom(c.id, BlankRoom({0, 0}), error)) {
      m_newName[0] = 0;
      m_editRequest = c;
    } else {
      m_status = error;
    }
  }
  ImGui::End();
}

void CampaignEditor::DrawRoomMap(Simulation &sim) {
  float s = View::UiScale();
  int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  for (RoomPos p : m_def.rooms) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  // A ring of space around for the '+' buttons
  --x0, --y0, ++x1, ++y1;
  float cell = 30 * s;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  float w = std::min((x1 - x0 + 1) * cell, ImGui::GetContentRegionAvail().x);
  ImGui::BeginChild("roommap", {w + 4, std::min((y1 - y0 + 1) * cell, 260 * s) + 4},
                    false, ImGuiWindowFlags_HorizontalScrollbar);
  origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy({(x1 - x0 + 1) * cell, (y1 - y0 + 1) * cell});
  ImDrawList *dl = ImGui::GetWindowDrawList();
  std::optional<RoomPos> go;
  std::optional<std::pair<int, int>> grow;
  auto rect = [&](RoomPos p) {
    ImVec2 a{origin.x + (p.x - x0) * cell + 2, origin.y + (p.y - y0) * cell + 2};
    return std::pair{a, ImVec2{a.x + cell - 4, a.y + cell - 4}};
  };
  for (RoomPos p : m_def.rooms) {
    auto [a, b] = rect(p);
    bool here = p == m_room.pos;
    dl->AddRectFilled(a, b, Theme::U32(here ? Tone::Brass : Tone::Line, 0.9f));
    if (p == m_def.startRoom)
      dl->AddText({a.x + 4, a.y + 2}, Theme::U32(Tone::Ink), "S");
    if (!here && ImGui::IsMouseHoveringRect(a, b)) {
      dl->AddRect(a, b, Theme::U32(Tone::BrassBright), 0.0f, 0, 2.0f);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        go = p;
    }
  }
  // '+' on each free side of the selected room
  for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
    RoomPos p = m_room.pos.Step(dx, dy);
    if (m_def.HasRoom(p))
      continue;
    auto [a, b] = rect(p);
    bool hover = ImGui::IsMouseHoveringRect(a, b);
    dl->AddRect(a, b, Theme::U32(hover ? Tone::BrassBright : Tone::BrassDim), 0.0f, 0, 1.5f);
    ImVec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    dl->AddLine({c.x - 6, c.y}, {c.x + 6, c.y}, Theme::U32(Tone::Brass), 2);
    dl->AddLine({c.x, c.y - 6}, {c.x, c.y + 6}, Theme::U32(Tone::Brass), 2);
    if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      grow = {dx, dy};
  }
  ImGui::EndChild();
  ImGui::TextDisabled("Room (%d, %d). Click a room to edit it, + to add one.",
                      m_room.pos.x, m_room.pos.y);
  if (m_room.pos != m_def.startRoom && Widgets::SmallButton("Delete this room"))
    DeleteRoom(sim);
  if (go && StoreRoom(sim))
    LoadRoomIntoWorld(sim, *go);
  if (grow)
    AddRoom(sim, grow->first, grow->second);
}

void CampaignEditor::DrawTools() {
  struct Entry {
    Tool tool;
    const char *label;
  };
  static const Entry kTools[] = {
      {Tool::Paint, "Paint"},   {Tool::Select, "Select"},   {Tool::Start, "Start"},
      {Tool::Gate, "Gate"},     {Tool::Workbench, "Bench"}, {Tool::Shrine, "Shrine"},
      {Tool::Mage, "Mage"},     {Tool::Undead, "Undead"},   {Tool::Flyer, "Flyer"},
  };
  float w = 100.0f * View::UiScale();
  int n = 0;
  for (const Entry &e : kTools) {
    if (Widgets::Button(e.label, {w, 0}, m_tool == e.tool)) {
      m_tool = e.tool;
      m_dragging = false;
    }
    if (++n % 3 != 0)
      ImGui::SameLine();
  }
  ImGui::TextDisabled(m_tool == Tool::Paint
                          ? "Left paints, right erases, wheel resizes."
                          : "Click to place, drag to move, Delete removes,\n"
                            "right click lets go.");
}

void CampaignEditor::DrawPaint(Simulation &sim) {
  int n = 0;
  for (Element e : kBrushElements) {
    ImGui::PushID(static_cast<int>(e));
    const char *label = e == Element::AIR ? "Erase" : ElementName(e);
    if (Widgets::Button(label, {100.0f * View::UiScale(), 0}, m_brushElement == e))
      m_brushElement = e;
    ImGui::PopID();
    if (++n % 3 != 0)
      ImGui::SameLine();
  }
  ImGui::NewLine();
  ImGui::SliderInt("Brush", &m_brush, 0, 20);
  if (Widgets::Button(m_running ? "Freeze the world" : "Let it settle"))
    m_running = !m_running;

  // Terrain from a 1v1 map
  if (m_maps.empty() && Widgets::SmallButton("Copy from a 1v1 map..."))
    m_maps = MapStore().LoadAll();
  if (!m_maps.empty()) {
    m_mapPick = std::clamp(m_mapPick, 0, (int)m_maps.size() - 1);
    if (ImGui::BeginCombo("Map", m_maps[m_mapPick].name.c_str())) {
      for (int i = 0; i < (int)m_maps.size(); ++i)
        if (ImGui::Selectable(m_maps[i].name.c_str(), i == m_mapPick))
          m_mapPick = i;
      ImGui::EndCombo();
    }
    if (Widgets::Button("Copy it here (replaces the terrain)")) {
      m_room.terrain.cells = m_maps[m_mapPick].cells;
      m_room.terrain.settings = m_maps[m_mapPick].settings;
      Maps::Build(sim, m_room.terrain, 1);
      m_status = "Copied " + m_maps[m_mapPick].name + ".";
    }
  }
  if (Widgets::SmallButton("Clear to a bare floor")) {
    RoomDef blank = BlankRoom(m_room.pos);
    m_room.terrain.cells = blank.terrain.cells;
    Maps::Build(sim, m_room.terrain, 1);
  }
}

void CampaignEditor::DrawSelected(UI &ui) {
  switch (m_selected.kind) {
  case Selection::None:
    ImGui::TextDisabled("Nothing selected.");
    return;
  case Selection::Start:
    ImGui::Text("Where a new game starts.");
    return;
  case Selection::Object: {
    ObjectDef &o = m_room.objects[m_selected.index];
    ImGui::Text("%s", ObjectName(o.kind));
    if (o.kind == ObjectKind::Gate)
      ImGui::TextDisabled("A checkpoint: touching it saves the way back,\n"
                          "and E travels between opened gates.");
    else if (o.kind == ObjectKind::Workbench)
      ImGui::TextDisabled("E opens the spell editor (unlocked glyphs only).");
    if (o.kind != ObjectKind::Shrine)
      break;
    GlyphDocs::Info info = GlyphDocs::Get(o.glyph);
    if (ImGui::BeginCombo("Teaches", info.name ? info.name : o.glyph.c_str())) {
      for (GlyphKind kind : {GlyphKind::Sigil, GlyphKind::Sign}) {
        ImGui::TextDisabled(kind == GlyphKind::Sigil ? "Sigils" : "Signs");
        for (const SvgAsset *a : ui.Glyphs().GetByKind(kind)) {
          GlyphDocs::Info gi = GlyphDocs::Get(a->id);
          if (ImGui::Selectable(gi.name ? gi.name : a->id.c_str(), a->id == o.glyph)) {
            o.glyph = a->id;
            o.sigil = kind == GlyphKind::Sigil;
          }
        }
      }
      ImGui::EndCombo();
    }
    break;
  }
  case Selection::Enemy: {
    EnemyDef &e = m_room.enemies[m_selected.index];
    ImGui::Text("%s", EnemyName(e.kind));
    ImGui::SliderFloat("Health", &e.hp, 5.0f, 300.0f, "%.0f");
    ImGui::SliderFloat("Speed", &e.speed, 5.0f, 80.0f, "%.0f cells/s");
    ImGui::SliderFloat("Touch damage", &e.damage, 0.0f, 50.0f, "%.0f");
    if (e.kind != EnemyKind::Mage)
      break;
    ImGui::SliderFloat("Casts every", &e.castEvery, 0.4f, 6.0f, "%.1fs");
    ImGui::TextDisabled("Spells it picks from at random (from your library):");
    ImGui::BeginChild("mage spells", {0, 160 * View::UiScale()}, true);
    for (const Spell &spell : ui.Library().All()) {
      auto it = std::find_if(e.spells.begin(), e.spells.end(),
                             [&](const Spell &s) { return s.name == spell.name; });
      bool has = it != e.spells.end();
      if (ImGui::Checkbox(spell.name.c_str(), &has)) {
        if (has)
          e.spells.push_back(spell);
        else
          e.spells.erase(it);
      }
    }
    ImGui::EndChild();
    if (e.spells.empty())
      ImGui::TextColored(Theme::Vec(Tone::Brass), "With no spells it only walks.");
    break;
  }
  }
  if (Widgets::SmallButton("Remove"))
    RemoveSelected();
}

void CampaignEditor::DrawRoomSettings() {
  ImGui::Text("Background: %s", m_room.background.empty() ? "none" : m_room.background.c_str());
  ImGui::TextDisabled("Drop a PNG on the window, or give its path:");
  ImGui::SetNextItemWidth(-80 * View::UiScale());
  ImGui::InputText("##bg", m_bgPath, sizeof m_bgPath);
  ImGui::SameLine();
  if (Widgets::SmallButton("Import")) {
    std::string error;
    if (auto file = ImportBackground(m_def.id, m_bgPath, error)) {
      SetBackground(*file);
      m_status = "Background set: " + *file;
    } else {
      m_status = error;
    }
  }
  // Ones this campaign already has
  std::error_code ec;
  std::string dir = Dir(m_def.id) + "/backgrounds";
  if (std::filesystem::exists(dir, ec))
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      std::string file = entry.path().filename().string();
      if (Widgets::SmallButton(file.c_str(), file == m_room.background))
        SetBackground(file);
      ImGui::SameLine();
    }
  ImGui::NewLine();
  if (!m_room.background.empty() && Widgets::SmallButton("No background"))
    SetBackground("");
}

void CampaignEditor::DrawStartingKit(UI &ui) {
  ImGui::TextDisabled("Glyphs a new game starts with; shrines teach the rest.");
  for (GlyphKind kind : {GlyphKind::Sigil, GlyphKind::Sign}) {
    int n = 0;
    for (const SvgAsset *a : ui.Glyphs().GetByKind(kind)) {
      GlyphDocs::Info info = GlyphDocs::Get(a->id);
      bool has = m_def.startingKit.count(a->id) > 0;
      if (n++ % 2)
        ImGui::SameLine(170 * View::UiScale());
      if (ImGui::Checkbox(info.name ? info.name : a->id.c_str(), &has)) {
        if (has)
          m_def.startingKit.insert(a->id);
        else
          m_def.startingKit.erase(a->id);
      }
    }
    ImGui::Separator();
  }
}
