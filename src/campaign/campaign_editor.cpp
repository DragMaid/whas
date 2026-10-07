#include "whas/campaign/campaign_editor.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "whas/campaign/campaign_play.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/character.h"
#include "whas/game/character_draw.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/glyph_docs.h"
#include "whas/ui/editor_icons.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include "whas/ui/widgets.h"
#include <algorithm>
#include <filesystem>

using namespace Campaign;
using Theme::Tone;

namespace {

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

constexpr Color NPC_TINT{235, 215, 160, 255};
constexpr const char *EDGE_NAMES[EDGES] = {"Left", "Right", "Up", "Down"};

// A glyph picker: "" for none
bool GlyphCombo(const char *label, std::string &glyph, bool &sigil, UI &ui,
                bool allowNone) {
  GlyphDocs::Info info = GlyphDocs::Get(glyph);
  const char *shown = glyph.empty() ? "(none)" : info.name ? info.name : glyph.c_str();
  bool changed = false;
  if (!ImGui::BeginCombo(label, shown))
    return false;
  if (allowNone && ImGui::Selectable("(none)", glyph.empty())) {
    glyph.clear();
    changed = true;
  }
  for (GlyphKind kind : {GlyphKind::Sigil, GlyphKind::Sign}) {
    ImGui::TextDisabled(kind == GlyphKind::Sigil ? "Sigils" : "Signs");
    for (const SvgAsset *a : ui.Glyphs().GetByKind(kind)) {
      GlyphDocs::Info gi = GlyphDocs::Get(a->id);
      if (ImGui::Selectable(gi.name ? gi.name : a->id.c_str(), a->id == glyph)) {
        glyph = a->id;
        sigil = kind == GlyphKind::Sigil;
        changed = true;
      }
    }
  }
  ImGui::EndCombo();
  return changed;
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
  m_regionFor = -1;
  m_brush.ClearHistory();
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

void CampaignEditor::DrawBackground() const {
  if (!m_editing || !m_background.id)
    return;
  DrawTexturePro(m_background,
                 {0, 0, (float)m_background.width, (float)m_background.height},
                 {0, 0, (float)(GRID_W * CELL_SIZE), (float)(GRID_H * CELL_SIZE)},
                 {0, 0}, 0.0f, WHITE);
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


CampaignEditor::Selection CampaignEditor::HitTest(Vector2 cell) const {
  for (int i = static_cast<int>(m_room.npcs.size()) - 1; i >= 0; --i)
    if (CheckCollisionPointRec(cell, BodyBox(m_room.npcs[i].pos)))
      return {Selection::Npc, i};
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

Vector2 &CampaignEditor::SelectedPos() {
  switch (m_selected.kind) {
  case Selection::Object:
    return m_room.objects[m_selected.index].pos;
  case Selection::Enemy:
    return m_room.enemies[m_selected.index].pos;
  case Selection::Npc:
    return m_room.npcs[m_selected.index].pos;
  default:
    return m_def.startPos;
  }
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
  case Tool::Npc: {
    NpcDef n;
    Vector2 floor = OnFloor(sim, cell);
    n.pos = {floor.x - Character::WIDTH * 0.5f, floor.y - Character::HEIGHT};
    m_room.npcs.push_back(n);
    m_selected = {Selection::Npc, static_cast<int>(m_room.npcs.size()) - 1};
    break;
  }
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
  else if (m_selected.kind == Selection::Npc)
    m_room.npcs.erase(m_room.npcs.begin() + m_selected.index);
  m_selected = {};
}

void CampaignEditor::Resume(Simulation &sim) {
  Maps::Build(sim, m_room.terrain, 1);
}

void CampaignEditor::HandleShortcuts(Simulation &sim) {
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;
  bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
  if (ctrl && IsKeyPressed(KEY_S)) {
    StoreRoom(sim);
    return;
  }
  if (m_tool == Tool::Paint)
    m_brush.HandleKeys(sim); // 1-9, E, [ ], Ctrl+Z
  else if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))
    RemoveSelected();
  if (ctrl)
    return;
  struct Key {
    KeyboardKey key;
    Tool tool;
  };
  static const Key keys[] = {
      {KEY_B, Tool::Paint},  {KEY_V, Tool::Select},    {KEY_T, Tool::Start},
      {KEY_G, Tool::Gate},   {KEY_W, Tool::Workbench}, {KEY_H, Tool::Shrine},
      {KEY_N, Tool::Npc},    {KEY_M, Tool::Mage},      {KEY_U, Tool::Undead},
      {KEY_F, Tool::Flyer},
  };
  for (const Key &k : keys)
    if (IsKeyPressed(k.key)) {
      m_tool = k.tool;
      m_dragging = false;
    }
  if (IsKeyPressed(KEY_ESCAPE))
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
  HandleShortcuts(sim);

  if (m_running) {
    m_accumulator += std::min(GetFrameTime(), 0.1f);
    while (m_accumulator >= TurnController::TICK_DT) {
      m_accumulator -= TurnController::TICK_DT;
      sim.Update(TurnController::TICK_DT);
    }
  }

  Vector2 cell = View::MouseCells();
  bool overUi = ImGui::GetIO().WantCaptureMouse;
  if (m_tool == Tool::Paint) {
    m_brush.Paint(sim, cell, overUi);
    return;
  }
  if (m_tool == Tool::Region) {
    if (m_regionFor < 0 || m_regionFor >= (int)m_room.conditions.size()) {
      m_tool = Tool::Select;
      return;
    }
    if (!overUi && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
      m_regionStart = cell;
    if (m_regionStart) {
      Vector2 a = *m_regionStart;
      m_room.conditions[m_regionFor].region = {
          std::floor(std::min(a.x, cell.x)), std::floor(std::min(a.y, cell.y)),
          std::ceil(std::abs(cell.x - a.x)) + 1, std::ceil(std::abs(cell.y - a.y)) + 1};
      if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        m_regionStart.reset();
        m_tool = Tool::Select;
        m_status = "Region set.";
      }
    }
    return;
  }

  if (m_dragging) {
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      m_dragging = false;
      if (m_selected.kind == Selection::Object)
        SelectedPos() = OnFloor(sim, SelectedPos());
      return;
    }
    SelectedPos() = {cell.x - m_dragOffset.x, cell.y - m_dragOffset.y};
    return;
  }
  if (overUi)
    return;
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
  Vector2 at = SelectedPos();
  m_dragOffset = {cell.x - at.x, cell.y - at.y};
  m_dragging = true;
}

void CampaignEditor::DrawWorld(const UI &ui) const {
  if (!m_editing)
    return;
  auto px = [](Rectangle r) {
    return Rectangle{r.x * CELL_SIZE, r.y * CELL_SIZE, r.width * CELL_SIZE,
                     r.height * CELL_SIZE};
  };
  Color mark = Theme::Rl(Tone::Oxblood, 0.95f);
  auto selected = [&](Selection::Kind kind, int i) {
    return m_selected.kind == kind && m_selected.index == i;
  };
  for (int i = 0; i < static_cast<int>(m_room.objects.size()); ++i) {
    DrawCampaignObject(m_room.objects[i], true, ui.Glyphs());
    if (selected(Selection::Object, i))
      DrawRectangleLinesEx(px(ObjectBox(m_room.objects[i])), 2, mark);
  }
  auto body = [&](Vector2 pos, Color tint, const char *label, bool sel) {
    Character c;
    c.pos = pos;
    c.grounded = true;
    DrawCharacterBody(c, tint, false);
    Rectangle r = px(BodyBox(pos));
    Theme::DrawText(Theme::RlBody(), label, {r.x - 8, r.y - 40}, 16, tint);
    if (sel)
      DrawRectangleLinesEx(r, 2, mark);
  };
  for (int i = 0; i < static_cast<int>(m_room.enemies.size()); ++i) {
    const EnemyDef &e = m_room.enemies[i];
    std::string label = e.tag.empty() ? EnemyName(e.kind)
                                      : std::string(EnemyName(e.kind)) + " (" + e.tag + ")";
    body(e.pos, EnemyTint(e.kind), label.c_str(), selected(Selection::Enemy, i));
  }
  for (int i = 0; i < static_cast<int>(m_room.npcs.size()); ++i)
    body(m_room.npcs[i].pos, NPC_TINT, m_room.npcs[i].name.c_str(),
         selected(Selection::Npc, i));
  if (m_room.pos == m_def.startRoom) {
    Rectangle r = px(BodyBox(m_def.startPos));
    DrawRectangleLinesEx(r, 2, Color{60, 140, 70, 255});
    Theme::DrawText(Theme::RlBody(), "START", {r.x - 10, r.y - 22}, 16,
                    Color{60, 140, 70, 255});
  }
  // Sealed edges and condition regions
  constexpr float W = GRID_W * CELL_SIZE, H = GRID_H * CELL_SIZE;
  Color seal{150, 90, 220, 220};
  if (m_room.sealed[EdgeLeft])
    DrawLineEx({3, 0}, {3, H}, 4, seal);
  if (m_room.sealed[EdgeRight])
    DrawLineEx({W - 3, 0}, {W - 3, H}, 4, seal);
  if (m_room.sealed[EdgeUp])
    DrawLineEx({0, 3}, {W, 3}, 4, seal);
  if (m_room.sealed[EdgeDown])
    DrawLineEx({0, H - 3}, {W, H - 3}, 4, seal);
  for (int i = 0; i < (int)m_room.conditions.size(); ++i) {
    const ConditionDef &c = m_room.conditions[i];
    if (c.kind != ConditionKind::Break && c.kind != ConditionKind::Fill)
      continue;
    Rectangle r = px(c.region);
    DrawRectangleRec(r, Color{150, 90, 220, 40});
    DrawRectangleLinesEx(r, i == m_regionFor ? 3.0f : 2.0f, seal);
    Theme::DrawText(Theme::RlBody(), TextFormat("%d: %s", i + 1, ConditionName(c.kind)),
                    {r.x + 4, r.y + 2}, 14, seal);
  }
  if (m_tool == Tool::Paint && !ImGui::GetIO().WantCaptureMouse)
    m_brush.DrawCursor(View::MouseCells());
}

void CampaignEditor::DrawPanel(Simulation &sim, UI &ui) {
  if (m_hub && !m_editing)
    DrawHub();
  if (auto def = std::exchange(m_editRequest, {}))
    Open(sim, *def);
  if (!m_editing)
    return;

  float s = View::UiScale();
  float panelW = 380.0f * s;
  DrawToolbar(sim, panelW);

  ImGui::SetNextWindowPos({GetScreenWidth() - 8.0f * s, 8.0f * s},
                          ImGuiCond_Always, {1.0f, 0.0f});
  ImGui::SetNextWindowSize({panelW, GetScreenHeight() - 16.0f * s},
                           ImGuiCond_Always);
  ImGui::Begin("Campaign editor", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##name", "Campaign name", m_name, sizeof m_name);
  if (Widgets::Button("Save"))
    StoreRoom(sim);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Ctrl+S");
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

  Widgets::SectionHeader("Rooms");
  DrawRoomMap(sim);

  if (ImGui::BeginTabBar("tabs")) {
    // Picking something in the room brings its settings up
    bool picked = m_selected.kind != Selection::None &&
                  (m_selected.kind != m_shown.kind || m_selected.index != m_shown.index);
    m_shown = m_selected;
    if (ImGui::BeginTabItem("Selected", nullptr,
                            picked ? ImGuiTabItemFlags_SetSelected : 0)) {
      DrawSelected(ui);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Room")) {
      DrawRoomTab(sim);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Campaign")) {
      DrawCampaignTab(ui);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// Tools along the top, and the paint palette when painting
void CampaignEditor::DrawToolbar(Simulation &sim, float panelWidth) {
  float s = View::UiScale();
  ImGui::SetNextWindowPos({8 * s, 8 * s}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({GetScreenWidth() - panelWidth - 24 * s, 0}, ImGuiCond_Always);
  ImGui::Begin("##toolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_AlwaysAutoResize);
  struct Entry {
    Tool tool;
    EditorIcon icon;
    const char *tip;
  };
  static const Entry kTools[] = {
      {Tool::Paint, EditorIcon::Brush, "Paint terrain (B)"},
      {Tool::Select, EditorIcon::Select, "Select and move (V)"},
      {Tool::Start, EditorIcon::Start, "Where a new game starts (T)"},
      {Tool::Gate, EditorIcon::Gate, "Windowway gate: checkpoint and travel (G)"},
      {Tool::Workbench, EditorIcon::Bench, "Workbench: draw spells (W)"},
      {Tool::Shrine, EditorIcon::Shrine, "Glyph shrine: teaches a glyph (H)"},
      {Tool::Npc, EditorIcon::Npc, "Someone to talk to (N)"},
      {Tool::Mage, EditorIcon::Mage, "Mage: casts its spells at you (M)"},
      {Tool::Undead, EditorIcon::Undead, "Undead: charges and knocks back (U)"},
      {Tool::Flyer, EditorIcon::Flyer, "Flyer: flies around walls to you (F)"},
  };
  int n = 0;
  for (const Entry &e : kTools) {
    ImGui::PushID(n++);
    if (IconButton("##tool", e.icon, m_tool == e.tool, e.tip)) {
      m_tool = e.tool;
      m_dragging = false;
    }
    ImGui::PopID();
    ImGui::SameLine(0, 4 * s);
  }
  ImGui::SameLine(0, 14 * s);
  if (IconButton("##settle", EditorIcon::Settle, m_running,
                 m_running ? "Freeze the world" : "Let it settle: run the world so water and sand come to rest"))
    m_running = !m_running;
  ImGui::SameLine(0, 14 * s);
  if (m_tool == Tool::Paint) {
    m_brush.DrawPalette();
    ImGui::SameLine();
    WrapToolbar(60 * s);
    ImGui::BeginDisabled(!m_brush.CanUndo());
    if (Widgets::SmallButton("Undo"))
      m_brush.Undo(sim);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Ctrl+Z");
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Tone::Faint));
    ImGui::TextWrapped("Left paints, right erases, wheel or [ ] sizes, 1-9 pick, E eraser");
    ImGui::PopStyleColor();
  } else if (m_tool == Tool::Region) {
    ImGui::TextColored(Theme::Vec(Tone::Oxblood),
                       "Drag a rectangle for condition %d", m_regionFor + 1);
  } else {
    ImGui::TextDisabled(m_tool == Tool::Select
                            ? "Click to select, drag to move, Delete removes"
                            : "Click to place, click something to move it, Delete removes");
  }
  ImGui::End();
}

void CampaignEditor::DrawSelected(UI &ui) {
  switch (m_selected.kind) {
  case Selection::None:
    ImGui::TextDisabled("Nothing selected. Click something in the room.");
    return;
  case Selection::Start:
    ImGui::Text("Where a new game starts.");
    return;
  case Selection::Npc:
    DrawNpc(m_room.npcs[m_selected.index], ui);
    break;
  case Selection::Object: {
    ObjectDef &o = m_room.objects[m_selected.index];
    Widgets::SectionHeader(ObjectName(o.kind));
    if (o.kind == ObjectKind::Gate)
      ImGui::TextWrapped("A checkpoint: touching it saves the way back, and E "
                         "travels between opened gates.");
    else if (o.kind == ObjectKind::Workbench)
      ImGui::TextWrapped("E opens the spell editor (unlocked glyphs only).");
    else
      GlyphCombo("Teaches", o.glyph, o.sigil, ui, false);
    break;
  }
  case Selection::Enemy: {
    EnemyDef &e = m_room.enemies[m_selected.index];
    Widgets::SectionHeader(EnemyName(e.kind));
    ImGui::InputTextWithHint("Tag", "for conditions, e.g. boss", &e.tag);
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
      ImGui::TextColored(Theme::Vec(Tone::Oxblood), "With no spells it only walks.");
    break;
  }
  }
  ImGui::Spacing();
  if (Widgets::SmallButton("Remove (Delete)"))
    RemoveSelected();
}

// An NPC: name, tag and the dialogue tree
void CampaignEditor::DrawNpc(NpcDef &npc, UI &ui) {
  Widgets::SectionHeader("Someone to talk to");
  ImGui::InputText("Name", &npc.name);
  ImGui::InputTextWithHint("Tag", "for \"talk to\" conditions", &npc.tag);
  ImGui::TextDisabled("Spells and enemies pass them by. Node 1 starts the talk;\n"
                      "a reply leads to another node or ends it.");
  int remove = -1;
  for (int i = 0; i < static_cast<int>(npc.dialogue.size()); ++i) {
    DialogueNode &d = npc.dialogue[i];
    ImGui::PushID(i);
    std::string title = TextFormat("Node %d: %.24s", i + 1, d.text.c_str());
    if (ImGui::CollapsingHeader(title.c_str(), i == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      ImGui::InputTextMultiline("##text", &d.text, {-1, 70 * View::UiScale()});
      for (int r = 0; r < static_cast<int>(d.replies.size()); ++r) {
        ImGui::PushID(r);
        DialogueReply &rep = d.replies[r];
        ImGui::SetNextItemWidth(170 * View::UiScale());
        ImGui::InputTextWithHint("##reply", "Reply", &rep.text);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90 * View::UiScale());
        std::string to = rep.next < 0 ? "Ends" : TextFormat("Node %d", rep.next + 1);
        if (ImGui::BeginCombo("##next", to.c_str())) {
          if (ImGui::Selectable("Ends", rep.next < 0))
            rep.next = -1;
          for (int k = 0; k < static_cast<int>(npc.dialogue.size()); ++k)
            if (ImGui::Selectable(TextFormat("Node %d", k + 1), rep.next == k))
              rep.next = k;
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (Widgets::SmallButton("x")) {
          d.replies.erase(d.replies.begin() + r);
          ImGui::PopID();
          break;
        }
        ImGui::PopID();
      }
      if (d.replies.size() < MAX_REPLIES && Widgets::SmallButton("+ Reply"))
        d.replies.push_back({"...", -1});
      if (d.replies.empty())
        ImGui::TextDisabled("No replies: the talk ends after this.");
      GlyphCombo("Teaches", d.teach, d.teachSigil, ui, true);
      if (i > 0 && Widgets::SmallButton("Delete node"))
        remove = i;
    }
    ImGui::PopID();
  }
  if (remove > 0) {
    npc.dialogue.erase(npc.dialogue.begin() + remove);
    for (DialogueNode &d : npc.dialogue)
      for (DialogueReply &r : d.replies)
        r.next = r.next == remove ? -1 : r.next > remove ? r.next - 1 : r.next;
  }
  if (Widgets::SmallButton("+ Node"))
    npc.dialogue.push_back({"...", {}, "", false});
}

void CampaignEditor::DrawRoomTab(Simulation &sim) {
  Widgets::SectionHeader("To leave this room");
  DrawConditions();

  Widgets::SectionHeader("Background");
  ImGui::Text("%s", m_room.background.empty() ? "None" : m_room.background.c_str());
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

  Widgets::SectionHeader("Terrain");
  if (m_maps.empty() && Widgets::SmallButton("Copy from a 1v1 map..."))
    m_maps = MapStore().LoadAll();
  if (!m_maps.empty()) {
    m_mapPick = std::clamp(m_mapPick, 0, (int)m_maps.size() - 1);
    ImGui::SetNextItemWidth(180 * View::UiScale());
    if (ImGui::BeginCombo("##map", m_maps[m_mapPick].name.c_str())) {
      for (int i = 0; i < (int)m_maps.size(); ++i)
        if (ImGui::Selectable(m_maps[i].name.c_str(), i == m_mapPick))
          m_mapPick = i;
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (Widgets::SmallButton("Copy here")) {
      m_room.terrain.cells = m_maps[m_mapPick].cells;
      m_room.terrain.settings = m_maps[m_mapPick].settings;
      Maps::Build(sim, m_room.terrain, 1);
      m_brush.ClearHistory();
      m_status = "Copied " + m_maps[m_mapPick].name + ".";
    }
  }
  if (Widgets::SmallButton("Clear to a bare floor")) {
    m_room.terrain.cells = BlankRoom(m_room.pos).terrain.cells;
    Maps::Build(sim, m_room.terrain, 1);
    m_brush.ClearHistory();
  }
}

void CampaignEditor::DrawConditions() {
  ImGui::TextDisabled("Sealed edges stay shut until every condition holds.");
  for (int e = 0; e < EDGES; ++e) {
    if (e)
      ImGui::SameLine();
    ImGui::Checkbox(EDGE_NAMES[e], &m_room.sealed[e]);
  }
  int remove = -1;
  for (int i = 0; i < static_cast<int>(m_room.conditions.size()); ++i) {
    ConditionDef &c = m_room.conditions[i];
    ImGui::PushID(i);
    ImGui::Separator();
    int kind = static_cast<int>(c.kind);
    const char *names[static_cast<int>(ConditionKind::Count)];
    for (int k = 0; k < static_cast<int>(ConditionKind::Count); ++k)
      names[k] = ConditionName(static_cast<ConditionKind>(k));
    ImGui::SetNextItemWidth(220 * View::UiScale());
    if (ImGui::Combo(TextFormat("%d", i + 1), &kind, names, IM_ARRAYSIZE(names)))
      c.kind = static_cast<ConditionKind>(kind);
    ImGui::SameLine();
    if (Widgets::SmallButton("x"))
      remove = i;
    switch (c.kind) {
    case ConditionKind::Defeat:
      ImGui::InputTextWithHint("Tag", "empty: every enemy", &c.tag);
      break;
    case ConditionKind::Break:
    case ConditionKind::Fill:
      if (Widgets::SmallButton(c.region.width > 0 ? "Redraw region" : "Draw region")) {
        m_tool = Tool::Region;
        m_regionFor = i;
      }
      if (c.kind == ConditionKind::Break) {
        float pct = c.share * 100.0f;
        if (ImGui::SliderFloat("Broken", &pct, 5.0f, 100.0f, "%.0f%%"))
          c.share = pct / 100.0f;
      } else {
        int el = static_cast<int>(c.element);
        const char *els[] = {"Air",  "Water", "Earth", "Fire",  "Steam", "Cloud", "Ice",
                             "Sand", "Rock",  "Wood",  "Grass", "Smoke"};
        if (ImGui::Combo("Element", &el, els, IM_ARRAYSIZE(els)))
          c.element = static_cast<Element>(el);
        ImGui::SliderInt("Cells", &c.amount, 1, 2000);
      }
      break;
    case ConditionKind::Talk: {
      ImGui::InputTextWithHint("NPC tag", "empty: the first NPC", &c.tag);
      int node = c.node + 1;
      if (ImGui::InputInt("Up to node", &node))
        c.node = std::max(0, node) - 1;
      ImGui::TextDisabled("Node 0: just talking is enough.");
      break;
    }
    default:
      break;
    }
    ImGui::InputTextWithHint("Hint", "shown to the player (optional)", &c.hint);
    ImGui::PopID();
  }
  if (remove >= 0) {
    m_room.conditions.erase(m_room.conditions.begin() + remove);
    m_regionFor = -1;
  }
  if (Widgets::SmallButton("+ Condition"))
    m_room.conditions.push_back({});
}

void CampaignEditor::DrawCampaignTab(UI &ui) {
  Widgets::SectionHeader("Starting kit");
  ImGui::TextDisabled("Glyphs a new game knows; shrines and NPCs teach the rest.");
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
