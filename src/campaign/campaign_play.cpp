#include "whas/campaign/campaign_play.h"
#include "imgui.h"
#include "whas/audio/audio_manager.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/game/character_draw.h"
#include "whas/game/match.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/glyph_docs.h"
#include "whas/spell/spell_quant.h"
#include "whas/ui/spell_thumbnails.h"
#include "whas/ui/theme.h"
#include "whas/ui/ui.h"
#include <algorithm>
#include <cmath>

using namespace Campaign;
using Theme::Tone;

namespace {

constexpr int PLAYER_ID = 1;
constexpr float REACH = 9.0f;        // cells to use an object
constexpr float RESPAWN_DELAY = 1.5f; // seconds lying there before respawning
constexpr int MAX_TICKS_PER_FRAME = 3;

float Dist(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Feet of a body standing on an object's spot
Vector2 StandAt(Vector2 spot) {
  return {spot.x - Character::WIDTH * 0.5f, spot.y - Character::HEIGHT};
}

// The nearest spot a body fits, searching outward (a room's edge may be
// walled where the last one was open)
Vector2 FreeNear(const Simulation &sim, Vector2 at) {
  if (Character::Fits(sim, at))
    return at;
  for (int r = 1; r < 60; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r)
          continue;
        Vector2 p{at.x + dx, at.y + dy};
        if (p.x >= 0 && p.y >= 0 && p.x + Character::WIDTH <= GRID_W &&
            p.y + Character::HEIGHT <= GRID_H && Character::Fits(sim, p))
          return p;
      }
  return at;
}

} // namespace

void DrawCampaignObject(const ObjectDef &o, bool lit, const SvgLibrary &glyphs) {
  float t = static_cast<float>(GetTime());
  Vector2 base{o.pos.x * CELL_SIZE, o.pos.y * CELL_SIZE};
  switch (o.kind) {
  case ObjectKind::Gate: {
    // TODO: Windowway sprite; a drawn window frame for now
    bool open = lit;
    Color frame = open ? Color{190, 230, 255, 255} : Color{120, 120, 140, 255};
    Rectangle r{base.x - 22, base.y - 76, 44, 76};
    if (open)
      DrawRectangleRec(r, Color{150, 210, 255, (unsigned char)(70 + 40 * std::sin(t * 3))});
    DrawRectangleLinesEx(r, 3, frame);
    DrawCircleSectorLines({base.x, r.y}, 22, 180, 360, 16, frame);
    DrawLineEx({base.x, r.y - 22}, {base.x, base.y}, 2, frame);
    DrawLineEx({r.x, r.y + 30}, {r.x + r.width, r.y + 30}, 2, frame);
    break;
  }
  case ObjectKind::Workbench: {
    // TODO: workbench sprite
    Color wood{140, 95, 55, 255};
    DrawRectangle((int)base.x - 26, (int)base.y - 26, 52, 8, wood);
    DrawRectangle((int)base.x - 22, (int)base.y - 18, 5, 18, wood);
    DrawRectangle((int)base.x + 17, (int)base.y - 18, 5, 18, wood);
    DrawRectangleLines((int)base.x - 14, (int)base.y - 34, 22, 8, Color{235, 225, 200, 255});
    break;
  }
  case ObjectKind::Shrine: {
    bool taken = !lit;
    Color c = taken ? Color{110, 105, 95, 255} : Color{240, 205, 120, 255};
    DrawRectangle((int)base.x - 10, (int)base.y - 20, 20, 20, Color{90, 85, 80, 255});
    Vector2 g{base.x, base.y - 44 + (taken ? 0.0f : 3.0f * std::sin(t * 2))};
    DrawCircleLinesV(g, 16, c);
    if (const SvgAsset *asset = glyphs.FindById(o.glyph)) {
      float k = 26.0f / std::max(asset->viewWidth, asset->viewHeight);
      for (const LineSeg &s : asset->segments)
        DrawLineEx({g.x + (s.a.x - asset->viewWidth * 0.5f) * k,
                    g.y + (s.a.y - asset->viewHeight * 0.5f) * k},
                   {g.x + (s.b.x - asset->viewWidth * 0.5f) * k,
                    g.y + (s.b.y - asset->viewHeight * 0.5f) * k},
                   2.0f, c);
    }
    break;
  }
  }
}

bool CampaignPlay::Start(Simulation &sim, UI &ui, const CampaignDef &def,
                         bool newGame, std::string &error) {
  if (!def.HasRoom(def.startRoom)) {
    error = "the campaign has no starting room";
    return false;
  }
  m_def = def;
  m_testing = false;
  if (newGame)
    ClearSave(def.id);
  std::optional<Save> save = newGame ? std::nullopt : LoadSave(def.id);
  m_save = save ? *save : Save{};
  if (!save)
    m_save.glyphs = def.startingKit;

  m_backpackSpells.SetDirectory(BackpackDir(def.id));
  m_backpackSpells.Load();
  m_bench.Bind(&m_backpackSpells, nullptr);
  m_bench.SetThumbnails(&ui.Thumbnails());
  m_bench.SetGlyphFilter(
      [this](const std::string &id) { return m_testing || m_save.glyphs.count(id) > 0; });

  m_active = true;
  m_player = Character{};
  m_player.id = PLAYER_ID;
  Respawn(sim);
  return true;
}

bool CampaignPlay::StartTest(Simulation &sim, UI &ui, const CampaignDef &def,
                             RoomPos room, Vector2 at, std::string &error) {
  if (!def.HasRoom(room)) {
    error = "no such room";
    return false;
  }
  m_def = def;
  m_testing = true;
  m_save = Save{};
  m_save.glyphs = def.startingKit;
  m_backpackSpells.SetDirectory(BackpackDir(def.id));
  m_backpackSpells.Load();
  m_bench.Bind(&m_backpackSpells, nullptr);
  m_bench.SetThumbnails(&ui.Thumbnails());
  m_bench.SetGlyphFilter([](const std::string &) { return true; });
  m_active = true;
  m_player = Character{};
  m_player.id = PLAYER_ID;
  m_player.maxHp = m_player.hp = Match::MAX_HP;
  EnterRoom(sim, room, at, false);
  return true;
}

void CampaignPlay::Stop() {
  if (!m_active)
    return;
  if (!m_testing)
    SaveProgress();
  m_active = false;
  m_cache.clear();
  if (m_background.id)
    UnloadTexture(m_background);
  m_background = {};
  m_enemies.clear();
  m_bench.Close();
  m_backpack = m_warp = false;
}

void CampaignPlay::Preload(RoomPos pos) {
  if (!m_def.HasRoom(pos) || m_cache.count(pos))
    return;
  std::string id = m_def.id;
  m_cache[pos] = std::async(std::launch::async, [id, pos] {
                   std::string error;
                   auto room = LoadRoom(id, pos, error);
                   if (!room)
                     room = BlankRoom(pos);
                   auto loaded = std::shared_ptr<Loaded>(new Loaded{std::move(*room), {}},
                                                         [](Loaded *l) {
                                                           if (l->background.data)
                                                             UnloadImage(l->background);
                                                           delete l;
                                                         });
                   if (!loaded->room.background.empty())
                     loaded->background =
                         LoadImage(BackgroundPath(id, loaded->room.background).c_str());
                   return loaded;
                 }).share();
}

std::shared_ptr<CampaignPlay::Loaded> CampaignPlay::Fetch(RoomPos pos) {
  Preload(pos);
  return m_cache.at(pos).get(); // already read, unless it was never preloaded
}

void CampaignPlay::EnterRoom(Simulation &sim, RoomPos pos, Vector2 at,
                             bool keepMotion) {
  std::shared_ptr<Loaded> loaded = Fetch(pos);
  m_room = loaded->room;
  Maps::Build(sim, m_room.terrain, static_cast<uint64_t>(m_rng()));
  if (m_background.id)
    UnloadTexture(m_background);
  m_background = loaded->background.data ? LoadTextureFromImage(loaded->background)
                                         : Texture2D{};

  Enemies::Spawn(m_room, sim, m_enemies);
  m_player.pos = FreeNear(sim, at);
  if (!keepMotion) {
    m_player.vel = {0, 0};
    m_player.pushX = 0;
  }
  m_player.Step(sim, {}, 0.0f);
  m_trail.Clear();
  m_save.visited.insert(pos);

  // Only this room and its neighbours stay read
  for (auto it = m_cache.begin(); it != m_cache.end();) {
    RoomPos p = it->first;
    if (std::abs(p.x - pos.x) + std::abs(p.y - pos.y) > 1)
      it = m_cache.erase(it);
    else
      ++it;
  }
  for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
    Preload(pos.Step(dx, dy));
  SaveProgress();
}

void CampaignPlay::Respawn(Simulation &sim) {
  m_player.maxHp = m_player.hp = Match::MAX_HP;
  m_player.burnStacks = m_player.burnExposure = 0;
  m_player.wet = 0.0f;
  m_player.flying = false;
  m_deadFor = 0.0f;
  m_cooldowns.Reset();
  if (m_save.respawn) {
    auto [room, index] = *m_save.respawn;
    auto def = Fetch(room);
    if (index >= 0 && index < static_cast<int>(def->room.objects.size())) {
      EnterRoom(sim, room, StandAt(def->room.objects[index].pos), false);
      return;
    }
  }
  EnterRoom(sim, m_def.startRoom, m_def.startPos, false);
}

void CampaignPlay::SaveProgress() {
  if (m_testing)
    return;
  std::string error;
  WriteSave(m_def.id, m_save, error);
}

void CampaignPlay::Notify(std::string text, float seconds) {
  m_notice = std::move(text);
  m_noticeTime = seconds;
}

const Spell *CampaignPlay::SlotSpell(int slot) const {
  if (slot < 0 || slot >= SLOTS || m_save.slots[slot].empty())
    return nullptr;
  return m_backpackSpells.Find(m_save.slots[slot]);
}

int CampaignPlay::NearObject(ObjectKind kind) const {
  for (int i = 0; i < static_cast<int>(m_room.objects.size()); ++i) {
    const ObjectDef &o = m_room.objects[i];
    Vector2 c{o.pos.x, o.pos.y - Character::HEIGHT * 0.5f};
    if (o.kind == kind && Dist(c, m_player.Center()) < REACH)
      return i;
  }
  return -1;
}

void CampaignPlay::Update(Simulation &sim, UI &ui, UIState &state) {
  if (!m_active)
    return;
  state.hideActionBar = true;
  state.matchRound = -1;
  m_noticeTime = std::max(0.0f, m_noticeTime - GetFrameTime());
  ui.Blind(m_player.TakeFlash(), false);

  bool keys = !ImGui::GetIO().WantCaptureKeyboard;
  if (keys && IsKeyPressed(KEY_ESCAPE)) {
    if (m_bench.IsOpen())
      m_bench.Close();
    else if (m_backpack || m_warp)
      m_backpack = m_warp = false;
    else
      m_exit = true;
  }
  if (keys && !m_bench.IsOpen() && IsKeyPressed(KEY_B)) {
    m_backpack = !m_backpack;
    m_warp = false;
  }
  // Pick up what the bench saved once it's put away
  if (m_benchWasOpen && !m_bench.IsOpen())
    m_backpackSpells.Load();
  m_benchWasOpen = m_bench.IsOpen();
  if (PanelOpen())
    return; // the world waits while the paper is out
  if (keys) {
    for (int i = 0; i < SLOTS; ++i)
      if (IsKeyPressed(KEY_ONE + i))
        m_slot = i;
    if (IsKeyPressed(KEY_E))
      Interact(sim);
  }

  CharacterInput input;
  if (keys && m_player.Alive()) {
    input.left = IsKeyDown(KEY_A);
    input.right = IsKeyDown(KEY_D);
    input.jump = IsKeyDown(KEY_W) || IsKeyDown(KEY_SPACE);
    input.down = IsKeyDown(KEY_S);
  }
  if (m_player.Alive())
    Cast(sim, ui);

  m_accumulator += std::min(GetFrameTime(), 0.1f);
  for (int n = 0; m_accumulator >= TurnController::TICK_DT && n < MAX_TICKS_PER_FRAME;
       ++n) {
    m_accumulator -= TurnController::TICK_DT;
    Tick(sim, ui, input, TurnController::TICK_DT);
    if (!m_active)
      return;
  }
  m_accumulator = std::min(m_accumulator, TurnController::TICK_DT);
  m_trail.Update(&m_player, 1, GetFrameTime());
}

void CampaignPlay::Cast(Simulation &sim, UI &ui) {
  const char *blocked = nullptr;
  auto target = CastTargeting::Update(sim, m_player.Center(), View::MouseCells(),
                                   !ui.IsBlockingWorldInput() &&
                                       !ImGui::GetIO().WantCaptureMouse,
                                   m_player.facing, &blocked);
  if (blocked)
    Notify(blocked, 1.5f);
  if (!target)
    return;
  const Spell *spell = SlotSpell(m_slot);
  if (!spell) {
    Notify("Nothing in that slot: press B for your backpack", 2.0f);
    return;
  }
  PlannedCast cast = PlannedCast::Local(*spell, target->aim);
  if (!cast.stats.valid) {
    Notify(SpellSystem::Problem(*spell), 2.5f);
    return;
  }
  if (!m_player.CanCast(cast.stats.HasFlight())) {
    Notify("Your spell paper is wet: only wind underfoot works", 1.5f);
    return;
  }
  if (!m_cooldowns.Ready(m_slot, m_tick)) {
    Notify("Still cooling down", 1.0f);
    return;
  }
  if (target->at && !cast.stats.HasFlight())
    cast.PlaceAt(*target->at, m_player.Center());
  m_cooldowns.Use(m_slot, m_tick, TurnController::CastTicks(cast.stats));
  sim.CastSpell(cast.stats, cast.Origin(m_player.Center()), cast.aim,
                m_player.id, cast.placed);
  if (cast.stats.HasFlight()) {
    m_player.wet = 0.0f;
    m_player.LaunchFlight(SpellSystem::FlightVelocity(cast.stats, cast.aim));
    AudioManager::EmitFlightLaunch(m_player.Center().x);
  }
}

void CampaignPlay::Tick(Simulation &sim, UI &ui, CharacterInput input, float dt) {
  (void)ui;
  ++m_tick;
  m_player.Unbury(sim);
  m_player.Step(sim, m_player.Alive() ? input : CharacterInput{}, dt);
  for (Enemies::Enemy &e : m_enemies)
    Enemies::Tick(e, sim, m_player, m_rng, dt);

  std::vector<Hurtbox> boxes;
  if (m_player.Alive())
    boxes.push_back({m_player.id, m_player.Bounds(), 0});
  for (const Enemies::Enemy &e : m_enemies)
    if (e.body.Alive())
      boxes.push_back({e.body.id, e.body.Bounds(), Enemies::TEAM});
  sim.GetParticleSystem().SetHurtboxes(std::move(boxes));
  sim.GetParticleSystem().SetCursors({{m_player.id, View::MouseCells()}});
  sim.Update(dt);

  // Hits, flashes, gusts and burning for everyone at once
  std::vector<Character> bodies{m_player};
  for (const Enemies::Enemy &e : m_enemies)
    bodies.push_back(e.body);
  Match::ApplyEffects(sim, bodies.data(), static_cast<int>(bodies.size()));
  m_player = bodies[0];
  for (size_t i = 0; i < m_enemies.size(); ++i)
    m_enemies[i].body = bodies[i + 1];
  if (m_tick % TurnController::TURN_TICKS == 0) {
    m_player.CoolBurn();
    for (Enemies::Enemy &e : m_enemies)
      e.body.CoolBurn();
  }

  // Shrines teach by touch; gates wake when walked through
  for (int i = 0; i < static_cast<int>(m_room.objects.size()); ++i) {
    const ObjectDef &o = m_room.objects[i];
    Vector2 c{o.pos.x, o.pos.y - Character::HEIGHT * 0.5f};
    if (!m_player.Alive() || Dist(c, m_player.Center()) > REACH * 0.7f)
      continue;
    std::pair<RoomPos, int> key{m_room.pos, i};
    if (o.kind == ObjectKind::Shrine && !m_save.shrinesTaken.count(key)) {
      m_save.shrinesTaken.insert(key);
      bool fresh = m_save.glyphs.insert(o.glyph).second;
      GlyphDocs::Info info = GlyphDocs::Get(o.glyph);
      std::string name = info.name ? info.name : o.glyph;
      Notify(fresh ? "Learned the " + name + (o.sigil ? " sigil" : " sign") +
                         ": draw it at a workbench"
                   : "You already know " + name,
             4.0f);
      SaveProgress();
    } else if (o.kind == ObjectKind::Gate &&
               (!m_save.respawn || *m_save.respawn != key)) {
      if (std::find(m_save.gates.begin(), m_save.gates.end(), key) ==
          m_save.gates.end()) {
        m_save.gates.push_back(key);
        Notify("The windowway opens: you'll return here. E to travel", 3.0f);
      }
      m_save.respawn = key;
      SaveProgress();
    }
  }

  if (!m_player.Alive()) {
    m_deadFor += dt;
    if (m_deadFor >= RESPAWN_DELAY)
      Respawn(sim);
    return;
  }
  CheckEdges(sim, input);
}

void CampaignPlay::CheckEdges(Simulation &sim, const CharacterInput &input) {
  constexpr float EDGE = 0.05f;
  Character &p = m_player;
  RoomPos at = m_room.pos;
  auto go = [&](int dx, int dy, Vector2 to) {
    if (!m_def.HasRoom(at.Step(dx, dy)))
      return false;
    EnterRoom(sim, at.Step(dx, dy), to, true);
    return true;
  };
  if (p.pos.x <= EDGE && (input.left || p.vel.x < 0.0f) &&
      go(-1, 0, {GRID_W - Character::WIDTH - 1.0f, p.pos.y}))
    return;
  if (p.pos.x + Character::WIDTH >= GRID_W - EDGE && (input.right || p.vel.x > 0.0f) &&
      go(1, 0, {1.0f, p.pos.y}))
    return;
  if (p.pos.y <= EDGE && p.vel.y <= 0.0f &&
      go(0, -1, {p.pos.x, GRID_H - Character::HEIGHT - 1.0f}))
    return;
  if (p.pos.y + Character::HEIGHT >= GRID_H - EDGE)
    go(0, 1, {p.pos.x, 1.0f});
}

void CampaignPlay::Interact(Simulation &sim) {
  m_warpSim = &sim;
  if (NearObject(ObjectKind::Workbench) >= 0) {
    m_bench.Open();
    return;
  }
  if (NearObject(ObjectKind::Gate) >= 0) {
    m_warp = true;
    m_backpack = false;
  }
}

void CampaignPlay::DrawBackground() const {
  if (!m_active || !m_background.id)
    return;
  DrawTexturePro(m_background,
                 {0, 0, (float)m_background.width, (float)m_background.height},
                 {0, 0, (float)(GRID_W * CELL_SIZE), (float)(GRID_H * CELL_SIZE)},
                 {0, 0}, 0.0f, WHITE);
}

void CampaignPlay::DrawWorld(const Simulation &sim, const UI &ui) const {
  if (!m_active)
    return;
  for (int i = 0; i < static_cast<int>(m_room.objects.size()); ++i) {
    const ObjectDef &o = m_room.objects[i];
    std::pair<RoomPos, int> key{m_room.pos, i};
    bool lit = o.kind == ObjectKind::Gate
                   ? std::find(m_save.gates.begin(), m_save.gates.end(), key) !=
                         m_save.gates.end()
                   : !m_save.shrinesTaken.count(key);
    DrawCampaignObject(o, lit, ui.Glyphs());
  }
  // What E would do here
  const char *prompt = NearObject(ObjectKind::Workbench) >= 0 ? "E  workbench"
                       : NearObject(ObjectKind::Gate) >= 0  ? "E  travel"
                                                             : nullptr;
  if (prompt && m_player.Alive())
    Theme::DrawText(Theme::RlBody(), prompt,
                    {m_player.Center().x * CELL_SIZE - 30, m_player.pos.y * CELL_SIZE - 50},
                    18, Theme::Rl(Tone::Parchment, 0.95f));

  m_trail.Draw();
  for (const Enemies::Enemy &e : m_enemies)
    Enemies::Draw(e);
  DrawCharacterBody(m_player, m_player.Alive() ? Color{230, 230, 240, 255} : GRAY, false);
  if (m_player.Alive() && !PanelOpen())
    CastTargeting::DrawWorld(sim, m_player.Center(), View::MouseCells());
  if (const Spell *spell = SlotSpell(m_slot); spell && m_player.Alive()) {
    Vector2 o = m_player.Center(), m = View::MouseCells();
    Vector2 aim{m.x - o.x, m.y - o.y};
    float len = std::hypot(aim.x, aim.y);
    if (len > 0.5f && !PanelOpen())
      ui.DrawAimIndicator(*spell, o, {aim.x / len, aim.y / len},
                          sim.GetConfig().world.gravity);
  }
}

void CampaignPlay::DrawPanels(UI &ui) {
  if (!m_active)
    return;
  DrawHud(ui);
  if (m_backpack)
    DrawBackpack(ui);
  if (m_warp)
    DrawWarp(m_warpSim);
  m_bench.Draw();
}

void CampaignPlay::DrawHud(UI &ui) {
  float s = View::UiScale();
  ImDrawList *dl = ImGui::GetForegroundDrawList();
  ImVec2 at{16 * s, 16 * s};
  // Health
  float w = 220 * s, h = 12 * s;
  dl->AddRectFilled(at, {at.x + w, at.y + h}, Theme::U32(Tone::Ink, 0.85f));
  dl->AddRectFilled(at, {at.x + w * (m_player.hp / m_player.maxHp), at.y + h},
                    IM_COL32(150, 176, 98, 255));
  dl->AddRect(at, {at.x + w, at.y + h}, Theme::U32(Tone::BrassDim));
  at.y += h + 8 * s;

  // The three slots, with their cooldowns and the wet paper
  float slot = 64 * s;
  for (int i = 0; i < SLOTS; ++i) {
    ImVec2 p0{at.x + i * (slot + 8 * s), at.y};
    ImVec2 p1{p0.x + slot, p0.y + slot};
    dl->AddRectFilled(p0, p1, Theme::U32(Tone::Ink, 0.85f));
    dl->AddRect(p0, p1, Theme::U32(i == m_slot ? Tone::BrassBright : Tone::BrassDim),
                0.0f, 0, i == m_slot ? 2.5f : 1.0f);
    const Spell *spell = SlotSpell(i);
    if (spell)
      ui.Thumbnails().Draw(dl, *spell, {p0.x + 3, p0.y + 3}, {p1.x - 3, p1.y - 3});
    float cool = m_cooldowns.Remaining(i, m_tick);
    if (cool > 0.0f)
      dl->AddRectFilled({p0.x, p1.y - slot * cool}, p1, Theme::U32(Tone::Ink, 0.7f));
    if (spell && m_player.Wet() && !SpellQuant::Canonical(*spell).HasFlight()) {
      float left = m_player.wet / Character::WET_SECONDS;
      dl->AddRectFilled({p0.x, p1.y - slot * left}, p1, IM_COL32(60, 130, 210, 110));
    }
    char key[2] = {static_cast<char>('1' + i), 0};
    dl->AddText({p0.x + 4 * s, p0.y + 2 * s}, Theme::U32(Tone::Parchment), key);
  }
  at.y += slot + 6 * s;
  dl->AddText(at, Theme::U32(Tone::Faint),
              TextFormat("%d glyphs known   B backpack   E use   right click: cast from a surface",
                         (int)m_save.glyphs.size()));
  if (m_testing)
    dl->AddText({at.x, at.y + 18 * s}, Theme::U32(Tone::Brass),
                "Testing from the editor: Esc to go back");

  if (m_noticeTime > 0.0f) {
    ImVec2 size = ImGui::CalcTextSize(m_notice.c_str());
    ImVec2 c{ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.18f};
    dl->AddRectFilled({c.x - size.x * 0.5f - 12, c.y - 8},
                      {c.x + size.x * 0.5f + 12, c.y + size.y + 8},
                      Theme::U32(Tone::Ink, 0.85f));
    dl->AddText({c.x - size.x * 0.5f, c.y}, Theme::U32(Tone::Parchment), m_notice.c_str());
  }
  if (!m_player.Alive()) {
    const char *text = "The ink runs out...";
    ImVec2 size = ImGui::CalcTextSize(text);
    ImVec2 c{ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.45f};
    dl->AddText({c.x - size.x * 0.5f, c.y}, Theme::U32(Tone::Oxblood), text);
  }
}

// The backpack (B): every spell made at a workbench; click one to put it in
// the selected slot
void CampaignPlay::DrawBackpack(UI &ui) {
  float s = View::UiScale();
  ImGui::SetNextWindowSize({560 * s, 420 * s}, ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(
      {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
      ImGuiCond_Appearing, {0.5f, 0.5f});
  if (!ImGui::Begin("Backpack", &m_backpack, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("Pick a slot, then a spell. New spells are drawn at workbenches.");
  for (int i = 0; i < SLOTS; ++i) {
    if (i)
      ImGui::SameLine();
    const Spell *spell = SlotSpell(i);
    std::string label = TextFormat("%d: %s##slot%d", i + 1,
                                   spell ? spell->name.c_str() : "empty", i);
    if (ImGui::Selectable(label.c_str(), m_slot == i, 0, {160 * s, 0}))
      m_slot = i;
  }
  if (ImGui::Button("Empty this slot")) {
    m_save.slots[m_slot].clear();
    SaveProgress();
  }
  ImGui::Separator();
  ImGui::BeginChild("spells");
  float card = 96 * s;
  int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (card + 8 * s)));
  int n = 0;
  for (const Spell &spell : m_backpackSpells.All()) {
    if (n++ % columns)
      ImGui::SameLine();
    ImGui::PushID(spell.name.c_str());
    ImVec2 p = ImGui::GetCursorScreenPos();
    std::string ref = m_backpackSpells.RefOf(spell);
    bool equipped = std::find(m_save.slots.begin(), m_save.slots.end(), ref) !=
                    m_save.slots.end();
    if (ImGui::Selectable("##card", equipped, 0, {card, card + 18 * s})) {
      m_save.slots[m_slot] = ref;
      SaveProgress();
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ui.Thumbnails().Draw(dl, spell, {p.x + 8, p.y}, {p.x + card - 8, p.y + card - 16});
    dl->AddText({p.x + 4, p.y + card - 12}, Theme::U32(Tone::Parchment),
                spell.name.c_str());
    ImGui::PopID();
  }
  if (m_backpackSpells.All().empty())
    ImGui::TextDisabled("Empty. Find a workbench (E) to draw your first spell.");
  ImGui::EndChild();
  ImGui::End();
}

// Travelling between gates: the rooms seen so far, gates marked; click one
void CampaignPlay::DrawWarp(Simulation *sim) {
  float s = View::UiScale();
  ImGui::SetNextWindowPos(
      {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f},
      ImGuiCond_Appearing, {0.5f, 0.5f});
  if (!ImGui::Begin("Windowway", &m_warp,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("Rooms you've been through. Pick a lit gate to travel.");
  int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  for (RoomPos p : m_save.visited) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  float cell = 34 * s;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy({(x1 - x0 + 1) * cell, (y1 - y0 + 1) * cell});
  ImDrawList *dl = ImGui::GetWindowDrawList();
  std::optional<std::pair<RoomPos, int>> pick;
  for (RoomPos p : m_save.visited) {
    ImVec2 a{origin.x + (p.x - x0) * cell, origin.y + (p.y - y0) * cell};
    ImVec2 b{a.x + cell - 4, a.y + cell - 4};
    bool here = p == m_room.pos;
    dl->AddRectFilled(a, b, Theme::U32(here ? Tone::BrassDim : Tone::Line, 0.9f));
    for (auto [room, index] : m_save.gates) {
      if (room != p)
        continue;
      ImVec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
      Theme::Diamond(dl, c, 7 * s, Theme::U32(Tone::Verdigris));
      if (ImGui::IsMouseHoveringRect(a, b)) {
        dl->AddRect(a, b, Theme::U32(Tone::BrassBright), 0.0f, 0, 2.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
          pick = {{room, index}};
      }
      break;
    }
  }
  ImGui::End();
  if (pick && sim) {
    m_warp = false;
    auto loaded = Fetch(pick->first);
    if (pick->second < (int)loaded->room.objects.size())
      EnterRoom(*sim, pick->first, StandAt(loaded->room.objects[pick->second].pos), false);
  }
}
