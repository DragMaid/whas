#include "whas/game/sandbox.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character_draw.h"
#include "whas/game/match.h"
#include "whas/ui/ui.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr Color AVATAR_COLOR{230, 230, 240, 255};
constexpr int MAX_TICKS_PER_FRAME = 4; // don't spiral after a hitch

Vector2 MouseCell() {
  Vector2 m = GetMousePosition();
  return {m.x / CELL_SIZE, m.y / CELL_SIZE};
}

bool Contains(Rectangle r, Vector2 p) {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y &&
         p.y <= r.y + r.height;
}

} // namespace

void Sandbox::EnsureAvatar(const Simulation &sim) {
  if (m_hasAvatar)
    return;
  m_avatar = {};
  m_avatar.id = 1;
  m_avatar.maxHp = m_avatar.hp = Match::MAX_HP;
  // Stand on whatever is under the middle of the screen
  PlaceAvatar(sim, {GRID_W * 0.5f, 10.0f});
  m_hasAvatar = true;
}

void Sandbox::PlaceAvatar(const Simulation &sim, Vector2 cellPos) {
  m_avatar.pos = {cellPos.x - Character::WIDTH * 0.5f,
                  cellPos.y - Character::HEIGHT * 0.5f};
  m_avatar.pos.x =
      std::clamp(m_avatar.pos.x, 0.0f, GRID_W - Character::WIDTH);
  m_avatar.pos.y =
      std::clamp(m_avatar.pos.y, 0.0f, GRID_H - Character::HEIGHT);
  m_avatar.vel = {0, 0};
  m_avatar.pushX = 0;
  m_avatar.Step(sim, {}, 0.0f);
  m_home = m_avatar.pos;
}

void Sandbox::ResetAvatar(const Simulation &sim) {
  m_avatar.pos = m_home;
  m_avatar.vel = {0, 0};
  m_avatar.pushX = 0;
  m_avatar.hp = m_avatar.maxHp;
  m_avatar.burnStacks = 0;
  m_avatar.burnExposure = 0;
  m_avatar.Step(sim, {}, 0.0f);
}

int Sandbox::QueuedTicks() const {
  int ticks = 0;
  for (const PlannedCast &c : m_queued)
    ticks += TurnController::CastTicks(c.stats);
  return ticks;
}

void Sandbox::ToggleTime() {
  m_stopped = !m_stopped;
  if (m_stopped)
    return;
  // Let time flow: fire the queue one after another, each waiting for the
  // previous one's cast time, as a turn would
  int at = m_tick + 1;
  for (PlannedCast &cast : m_queued) {
    m_scheduled.push_back({cast, at});
    at += TurnController::CastTicks(cast.stats);
  }
  m_queued.clear();
}

Vector2 Sandbox::AimAtMouse() const {
  Vector2 o = m_avatar.Center();
  Vector2 m = MouseCell();
  Vector2 aim{m.x - o.x, m.y - o.y};
  float len = std::hypot(aim.x, aim.y);
  return len > 0.001f ? Vector2{aim.x / len, aim.y / len}
                      : Vector2{(float)m_avatar.facing, 0.0f};
}

void Sandbox::Fire(Simulation &sim, const PlannedCast &cast) {
  if (cast.stats.kind == SpellKind::Flight) {
    Vector2 dir = SpellSystem::ResolveDirection(cast.stats, cast.aim);
    m_avatar.Launch({dir.x * cast.stats.launchSpeed,
                     dir.y * cast.stats.launchSpeed});
  } else {
    sim.CastSpell(cast.stats, m_avatar.Center(), cast.aim, m_avatar.id);
  }
}

void Sandbox::Update(Simulation &sim, UI &ui, UIState &state) {
  EnsureAvatar(sim);
  state.matchRound = -1;

  bool keyboardFree = !ImGui::GetIO().WantCaptureKeyboard;
  if (state.timeToggleRequested || (keyboardFree && IsKeyPressed(KEY_SPACE)))
    ToggleTime();
  state.timeToggleRequested = false;
  if (state.resetAvatarRequested)
    ResetAvatar(sim);
  state.resetAvatarRequested = false;

  bool isPainting = false;
  if (state.tool == SandboxTool::Draw) {
    m_dragging = false;
    HandleDraw(sim, ui, state);
    isPainting =
        IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui.IsBlockingWorldInput();
  } else {
    HandleCast(sim, ui);
  }

  if (!m_stopped) {
    m_accumulator += std::min(GetFrameTime(), 0.1f);
    int steps = 0;
    while (m_accumulator >= TurnController::TICK_DT &&
           steps < MAX_TICKS_PER_FRAME) {
      Tick(sim, isPainting);
      m_accumulator -= TurnController::TICK_DT;
      ++steps;
    }
    if (steps == MAX_TICKS_PER_FRAME)
      m_accumulator = 0.0f;
  }

  state.clock = m_stopped ? ClockLook::Stopped : ClockLook::Running;
  state.clockProgress =
      m_stopped ? std::min(1.0f, QueuedTicks() / (float)TurnController::TURN_TICKS)
                : 0.0f;
  state.ticksFree = TurnController::TURN_TICKS;
}

void Sandbox::HandleDraw(Simulation &sim, UI &ui, UIState &state) {
  if (ui.IsBlockingWorldInput())
    return;
  Vector2 cell = ui.GetMouseCell();
  int cx = (int)cell.x;
  int cy = (int)cell.y;
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    sim.Paint(cx, cy, state.selectedMaterial, state.brushRadius);
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    sim.Erase(cx, cy, state.brushRadius);
}

void Sandbox::HandleCast(Simulation &sim, UI &ui) {
  Vector2 mouse = MouseCell();

  if (m_dragging) {
    m_avatar.pos = {mouse.x - m_dragOffset.x, mouse.y - m_dragOffset.y};
    m_avatar.vel = {0, 0};
    m_avatar.pushX = 0;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      m_dragging = false;
      PlaceAvatar(sim, m_avatar.Center());
    }
    return;
  }
  if (ui.IsBlockingWorldInput())
    return;

  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    PlaceAvatar(sim, mouse);
    return;
  }
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    return;

  // Grab the avatar (a little slack so it's easy to hit)
  Rectangle grab = m_avatar.Bounds();
  grab.x -= 1;
  grab.y -= 1;
  grab.width += 2;
  grab.height += 2;
  if (Contains(grab, mouse)) {
    m_dragging = true;
    m_dragOffset = {mouse.x - m_avatar.pos.x, mouse.y - m_avatar.pos.y};
    return;
  }

  const Spell *spell = ui.GetSelectedSpell();
  if (!spell || !SpellSystem::Evaluate(*spell).valid)
    return;
  PlannedCast cast = PlannedCast::Local(*spell, AimAtMouse());
  if (m_stopped)
    m_queued.push_back(std::move(cast));
  else
    Fire(sim, cast);
}

void Sandbox::Tick(Simulation &sim, bool isPainting) {
  ++m_tick;
  for (auto it = m_scheduled.begin(); it != m_scheduled.end();) {
    if (it->tick <= m_tick) {
      Fire(sim, it->cast);
      it = m_scheduled.erase(it);
    } else {
      ++it;
    }
  }

  if (!m_dragging)
    m_avatar.Step(sim, {}, TurnController::TICK_DT);
  sim.GetParticleSystem().SetHurtboxes({{m_avatar.id, m_avatar.Bounds()}});
  sim.Update(TurnController::TICK_DT, isPainting);
  Match::ApplyEffects(sim, &m_avatar, 1);
  // Practice dummy: never stays down
  if (!m_avatar.Alive())
    m_avatar.hp = 1.0f;
}

void Sandbox::Draw(const Simulation &sim, const UI &ui,
                   const UIState &state) const {
  if (!m_hasAvatar)
    return;
  DrawCharacterBody(m_avatar, AVATAR_COLOR, true);

  float gravity = sim.GetConfig().world.gravity;
  // Queued casts wait at the avatar, drawn as the beams they'll become
  for (const PlannedCast &cast : m_queued) {
    Vector2 dir = SpellSystem::ResolveDirection(cast.stats, cast.aim);
    Color c = ui.GetSpellColor(cast.spell);
    ui.DrawSpellBeam(cast.stats, m_avatar.Center(), dir,
                     Color{c.r, c.g, c.b, 200}, gravity);
  }

  if (state.tool != SandboxTool::Cast || m_dragging ||
      ui.IsBlockingWorldInput())
    return;
  Rectangle grab = m_avatar.Bounds();
  if (Contains({grab.x - 1, grab.y - 1, grab.width + 2, grab.height + 2},
               MouseCell())) {
    Rectangle r{grab.x * CELL_SIZE - 3, grab.y * CELL_SIZE - 3,
                grab.width * CELL_SIZE + 6, grab.height * CELL_SIZE + 6};
    DrawRectangleLinesEx(r, 1.5f, Color{130, 170, 255, 220});
    DrawText("drag", (int)r.x, (int)(r.y - 14), 12, Color{130, 170, 255, 220});
    return;
  }
  if (const Spell *spell = ui.GetSelectedSpell())
    ui.DrawAimIndicator(*spell, m_avatar.Center(), AimAtMouse(), gravity);
  if (m_stopped)
    DrawText(TextFormat("TIME STOPPED - %d queued, Space to release",
                        (int)m_queued.size()),
             12, 12, 18, Color{110, 210, 255, 255});
}
