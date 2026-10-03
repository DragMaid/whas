#include "whas/engine/view.h"
#include "whas/game/sandbox.h"
#include "imgui.h"
#include "whas/audio/audio_manager.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/arena_gen.h"
#include "whas/game/character_draw.h"
#include "whas/game/match.h"
#include "whas/ui/ui.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <cmath>

using Theme::Tone;

namespace {

constexpr Color AVATAR_COLOR{230, 230, 240, 255};
// One world step per frame at most. Catching up with extra steps makes a
// slow frame slower still, and the game never recovers; when a step takes
// longer than a frame the sandbox runs in slow motion instead.
constexpr int MAX_TICKS_PER_FRAME = 1;

Vector2 MouseCell() { return View::MouseCells(); }

bool Contains(Rectangle r, Vector2 p) {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y &&
         p.y <= r.y + r.height;
}

} // namespace

void Sandbox::EnsureAvatar(Simulation &sim) {
  if (m_hasAvatar)
    return;
  // The bottom rows sit behind the action bar: give the world an earth bed
  // there so nothing rests out of sight. Earth, not loose rock: a rock floor
  // becomes one huge body that wobbles and thumps
  for (int y = ArenaGen::FLOOR_BOTTOM - 3; y < GRID_H; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (sim.GetCell(x, y).element == Element::AIR)
        sim.Paint(x, y, Element::EARTH, 0);
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
  m_avatar.PlaceClear(sim);
  m_home = m_avatar.pos;
}

void Sandbox::ResetAvatar(const Simulation &sim) {
  m_avatar.pos = m_home;
  m_avatar.vel = {0, 0};
  m_avatar.pushX = 0;
  m_avatar.hp = m_avatar.maxHp;
  m_avatar.burnStacks = 0;
  m_avatar.burnExposure = 0;
  m_avatar.wet = 0.0f;
  m_avatar.PlaceClear(sim);
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
  if (!m_avatar.CanCast(cast.stats.HasFlight()))
    return;
  if (cast.stats.HasFlight())
    m_avatar.wet = 0.0f;
  sim.CastSpell(cast.stats, cast.Origin(m_avatar.Center()), cast.aim,
                m_avatar.id, cast.placed);
  if (cast.stats.HasFlight()) {
    m_avatar.LaunchFlight(SpellSystem::FlightVelocity(cast.stats, cast.aim));
    AudioManager::EmitFlightLaunch(m_avatar.Center().x);
  }
}

void Sandbox::Update(Simulation &sim, UI &ui, UIState &state) {
  // Light bursts fade at once here (and a match's hold is let go)
  ui.Blind(m_avatar.TakeFlash(), false);
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
    // Time we couldn't keep up with is dropped, not owed
    m_accumulator = std::min(m_accumulator, TurnController::TICK_DT);
  }
  m_trail.Update(&m_avatar, m_hasAvatar ? 1 : 0, GetFrameTime());

  state.clock = m_stopped ? ClockLook::Stopped : ClockLook::Running;
  state.clockProgress =
      m_stopped ? std::min(1.0f, QueuedTicks() / (float)TurnController::TURN_TICKS)
                : 0.0f;
  state.ticksFree = TurnController::TURN_TICKS;
  state.wet = m_avatar.wet / Character::WET_SECONDS;
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

  // Shift + right click moves the dummy (right click alone casts from a
  // surface)
  bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
  if (shift && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    PlaceAvatar(sim, mouse);
    return;
  }

  // Grab the avatar (a little slack so it's easy to hit)
  Rectangle grab = m_avatar.Bounds();
  grab.x -= 1;
  grab.y -= 1;
  grab.width += 2;
  grab.height += 2;
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      Contains(grab, mouse)) {
    m_dragging = true;
    m_dragOffset = {mouse.x - m_avatar.pos.x, mouse.y - m_avatar.pos.y};
    return;
  }

  const char *blocked = nullptr;
  auto target = CastTargeting::Update(sim, m_avatar.Center(), mouse, true,
                                   m_avatar.facing, &blocked);
  const Spell *spell = ui.GetSelectedSpell();
  if (!target || !spell || !SpellSystem::Evaluate(*spell).valid)
    return;
  PlannedCast cast = PlannedCast::Local(*spell, target->aim);
  if (target->at)
    cast.PlaceAt(*target->at, m_avatar.Center());
  if (m_stopped) {
    m_queued.push_back(std::move(cast));
    if (AudioManager *audio = AudioManager::Instance())
      audio->PlayUi(UiSound::SpellPlan);
  } else
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

  if (!m_dragging) {
    m_avatar.Unbury(sim);
    m_avatar.Step(sim, {}, TurnController::TICK_DT);
  }
  sim.GetParticleSystem().SetHurtboxes({{m_avatar.id, m_avatar.Bounds()}});
  // Sights set follows the live cursor here (casts without the avatar
  // have no owner)
  sim.GetParticleSystem().SetCursors(
      {{m_avatar.id, MouseCell()}, {-1, MouseCell()}});
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
  m_trail.Draw();
  DrawCharacterBody(m_avatar, AVATAR_COLOR, true);

  float gravity = sim.GetConfig().world.gravity;
  // Queued casts wait at the avatar, drawn as the beams they'll become
  for (const PlannedCast &cast : m_queued) {
    Vector2 dir = SpellSystem::ResolveDirection(cast.stats, cast.aim);
    Color c = ui.GetSpellColor(cast.spell);
    ui.DrawSpellBeam(cast.stats, cast.Origin(m_avatar.Center()), dir,
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
    DrawRectangleLinesEx(r, 1.5f, Theme::Rl(Tone::Brass, 0.85f));
    Theme::DrawText(Theme::RlBody(), "drag", {r.x, r.y - 18}, 16,
                    Theme::Rl(Tone::Brass, 0.85f));
    return;
  }
  CastTargeting::DrawWorld(sim, m_avatar.Center(), MouseCell());
  if (const Spell *spell = ui.GetSelectedSpell())
    ui.DrawAimIndicator(*spell, m_avatar.Center(), AimAtMouse(), gravity);
  if (m_stopped)
    Theme::DrawText(Theme::RlHeading(),
                    TextFormat("TIME STOPPED  -  %d queued, Space to release",
                               (int)m_queued.size()),
                    {14, 12}, 18, Theme::Rl(Tone::Verdigris));
}
