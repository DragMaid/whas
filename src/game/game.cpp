#include "whas/game/game.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/spell/spell_system.h"
#include "whas/ui/ui.h"
#include <algorithm>
#include <cmath>

namespace {

// Damage dealt per unit of projectile power
constexpr float DAMAGE_PER_POWER = 0.03f;

// Keep characters above the (sandbox) toolbar area
constexpr int FLOOR_TOP = 150;
constexpr int FLOOR_BOTTOM = 164;

Vector2 ToScreen(Vector2 cells) {
  return {cells.x * CELL_SIZE, cells.y * CELL_SIZE};
}

Rectangle ToScreen(Rectangle cells) {
  return {cells.x * CELL_SIZE, cells.y * CELL_SIZE, cells.width * CELL_SIZE,
          cells.height * CELL_SIZE};
}

Color WithAlpha(Color c, unsigned char a) { return {c.r, c.g, c.b, a}; }

float WorldGravity(const Simulation &sim) {
  return sim.GetConfig().world.gravity;
}

} // namespace

void Game::SetActive(bool active, Simulation &sim, UI &ui) {
  m_active = active;
  ui.SetGameMode(active);
  if (!active)
    return;

  if (!m_arenaReady) {
    m_slots[LOCAL].spawn = {60.0f, FLOOR_TOP - Character::HEIGHT};
    m_slots[LOCAL].maxHp = 100.0f;
    m_slots[LOCAL].color = {230, 230, 240, 255};
    m_slots[OPPONENT].spawn = {240.0f, FLOOR_TOP - Character::HEIGHT};
    m_slots[OPPONENT].maxHp = 200.0f;
    m_slots[OPPONENT].color = {220, 80, 80, 255};

    // Keep whatever was built in the sandbox; only fill empty ground
    SetupTerrain(sim);
    Respawn(sim, LOCAL);
    Respawn(sim, OPPONENT);
    m_arenaReady = true;
  }
  EnterWaiting();
}

void Game::SetupTerrain(Simulation &sim) {
  // A floor to stand on and a small earth mound between the two players.
  // Only fills empty cells so sandbox work is kept.
  for (int y = FLOOR_TOP; y <= FLOOR_BOTTOM; ++y)
    for (int x = 0; x < GRID_W; ++x)
      if (sim.GetCell(x, y).element == Element::AIR)
        sim.Paint(x, y, Element::EARTH, 0);

  for (int y = FLOOR_TOP - 12; y < FLOOR_TOP; ++y)
    for (int x = 150; x < 158; ++x)
      if (sim.GetCell(x, y).element == Element::AIR)
        sim.Paint(x, y, Element::EARTH, 0);
}

void Game::ResetArena(Simulation &sim) {
  sim.Reset();
  SetupTerrain(sim);
  Respawn(sim, LOCAL);
  Respawn(sim, OPPONENT);
  m_round = 1;
  m_turnNumber = 1;
  m_state = RoundState::Playing;
  EnterWaiting();
}

void Game::Respawn(Simulation &sim, int slot) {
  Slot &s = m_slots[slot];
  Character c;
  c.id = slot + 1; // hurtbox ids; 0 is never used
  c.pos = s.spawn;
  c.facing = slot == LOCAL ? 1 : -1;
  c.maxHp = c.hp = s.maxHp;
  c.Step(sim, {}, 0.0f); // resolve grounded before the first plan
  s.character = c;
}

void Game::BeginPlanning(Simulation &sim) {
  m_turn.BeginPlanning(m_slots[LOCAL].character);

  // The opponent's plan will arrive over the network; for now it's empty
  Slot &opponent = m_slots[OPPONENT];
  opponent.plan = {};
  opponent.preview = PreviewPlan(sim, opponent.character, opponent.plan,
                                 TurnController::TICK_DT);
}

void Game::EnterWaiting() { m_waiting = true; }

void Game::Commit() {
  m_slots[LOCAL].plan = m_turn.LocalPlan();
  m_turn.BeginExecution();
}

void Game::UpdateWaiting(Simulation &sim) {
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;

  if (IsKeyPressed(KEY_R)) {
    Respawn(sim, OPPONENT);
    Notify("Opponent reset", 1.5f);
  }

  // Stop time: start planning this turn
  if (IsKeyPressed(KEY_SPACE)) {
    m_waiting = false;
    m_paused = true; // Space just stopped time
    BeginPlanning(sim);
  }
}

void Game::Notify(const char *text, float seconds) {
  m_notice = text;
  m_noticeTime = seconds;
}

void Game::Update(Simulation &sim, UI &ui) {
  float frame = GetFrameTime();
  m_noticeTime = std::max(0.0f, m_noticeTime - frame);
  m_bannerTime = std::max(0.0f, m_bannerTime - frame);

  if (m_state == RoundState::Defeated) {
    if (IsKeyPressed(KEY_ENTER))
      ResetArena(sim);
    return;
  }

  if (m_waiting)
    UpdateWaiting(sim);
  else if (m_turn.GetPhase() == TurnController::Phase::Planning)
    UpdatePlanning(sim, ui);
  else
    UpdateExecuting(sim);
}

void Game::UpdatePlanning(Simulation &sim, UI &ui) {
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;

  if (IsKeyPressed(KEY_R)) {
    Respawn(sim, OPPONENT);
    BeginPlanning(sim);
    m_paused = true;
    Notify("Opponent reset", 1.5f);
    return;
  }

  if (IsKeyPressed(KEY_BACKSPACE)) {
    m_turn.Undo(sim);
    m_paused = true; // look at the result before time runs again
  }

  // Execute early: fire anything still queued, then go
  if (IsKeyPressed(KEY_ENTER)) {
    m_turn.Flush(sim);
    Commit();
    return;
  }

  // Pause / unpause the planning clock
  if (IsKeyPressed(KEY_SPACE)) {
    m_paused = !m_paused;
    if (m_paused)
      m_turn.MarkPause();
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ui.IsBlockingWorldInput()) {
    const Spell *spell = ui.GetSelectedSpell();
    const Character &ghost = m_turn.LocalPreview().end;
    if (!spell) {
      Notify("Select a spell in the Spells window first", 2.0f);
    } else if (!SpellSystem::Evaluate(*spell).valid) {
      Notify("That spell needs exactly one known sigil", 2.0f);
    } else {
      Vector2 origin = ghost.Center();
      Vector2 mouse = GetMousePosition();
      Vector2 aim{mouse.x / CELL_SIZE - origin.x, mouse.y / CELL_SIZE - origin.y};
      float len = std::hypot(aim.x, aim.y);
      aim = len > 0.001f ? Vector2{aim.x / len, aim.y / len}
                         : Vector2{(float)ghost.facing, 0.0f};
      switch (m_turn.QueueCast(*spell, aim)) {
      case TurnController::CastResult::Queued:
        break;
      case TurnController::CastResult::NoTime:
        Notify("Not enough time left in this turn to cast that", 2.0f);
        break;
      case TurnController::CastResult::SecondFlight:
        Notify("Only one wind sigil (movement) cast per pause", 2.0f);
        break;
      }
    }
  }

  // While paused nothing moves; unpaused, the clock runs in real time whether
  // or not the player does anything
  if (!m_paused) {
    CharacterInput input;
    input.left = IsKeyDown(KEY_A);
    input.right = IsKeyDown(KEY_D);
    input.jump = IsKeyDown(KEY_W);
    m_turn.FlowTick(sim, input);
  }

  // Prep time is over once the clock has run the whole turn
  if (m_turn.Finished())
    Commit();
}

void Game::UpdateExecuting(Simulation &sim) {
  int tick = m_turn.ExecutedTicks();
  for (Slot &slot : m_slots)
    TurnController::ApplyPlanTick(slot.plan, tick, sim, slot.character);

  std::vector<Hurtbox> hurtboxes;
  for (const Slot &slot : m_slots)
    if (slot.character.Alive())
      hurtboxes.push_back({slot.character.id, slot.character.Bounds()});
  sim.GetParticleSystem().SetHurtboxes(std::move(hurtboxes));

  sim.Update(TurnController::TICK_DT);
  ApplyHits(sim);
  ApplyGusts(sim);
  for (Slot &slot : m_slots)
    slot.character.UpdateBurn(sim, TurnController::TICK_DT);

  if (!m_turn.Advance())
    FinishTurn(sim);
}

void Game::FinishTurn(Simulation &sim) {
  m_turnNumber++;
  for (Slot &slot : m_slots)
    slot.character.CoolBurn();

  if (!m_slots[LOCAL].character.Alive()) {
    m_state = RoundState::Defeated;
    m_banner = "DEFEATED";
    m_bannerTime = 0.0f; // stays up until Enter
    return;
  }

  if (!m_slots[OPPONENT].character.Alive()) {
    m_round++;
    Respawn(sim, OPPONENT);
    m_banner = "VICTORY";
    m_bannerTime = 2.5f;
  }

  EnterWaiting();
}

// Gust fields push characters like everything else: acceleration = force/mass
void Game::ApplyGusts(const Simulation &sim) {
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Gust)
      continue;
    for (Slot &slot : m_slots) {
      Character &c = slot.character;
      if (c.id == effect.owner)
        continue;
      float strength = SpellSystem::GustStrengthAt(effect, c.Center());
      if (strength <= 0.0f)
        continue;
      float dv = effect.stats.force * strength / Character::MASS *
                 TurnController::TICK_DT;
      c.Launch({effect.direction.x * dv, effect.direction.y * dv});
    }
  }
}

void Game::ApplyHits(Simulation &sim) {
  for (const ParticleHit &hit : sim.GetParticleSystem().TakeHits()) {
    for (Slot &slot : m_slots) {
      Character &c = slot.character;
      if (c.id != hit.targetId)
        continue;
      c.hp = std::max(0.0f, c.hp - hit.power * DAMAGE_PER_POWER);
      if (hit.element == Element::WATER)
        c.burnStacks = 0;
    }
  }
}

void Game::Draw(const Simulation &sim, const UI &ui) const {
  bool planning = m_state == RoundState::Playing && !m_waiting &&
                  m_turn.GetPhase() == TurnController::Phase::Planning;

  for (int i = 0; i < (int)m_slots.size(); ++i) {
    const Slot &slot = m_slots[i];
    const Character &c = slot.character;
    Color body = c.Alive() ? slot.color : GRAY;
    // While planning, the real body is where the turn starts: draw it dimmed
    DrawCharacter(c, planning ? WithAlpha(body, 110) : body, true);

    if (!planning)
      continue;
    if (i == LOCAL) {
      DrawSlotPlan(slot, m_turn.LocalPreview(), sim, ui);
      DrawPendingCasts(sim, ui);
    } else {
      DrawSlotPlan(slot, slot.preview, sim, ui);
    }
  }

  // Live aim from where the local player will be when this cast would fire
  if (planning && !ui.IsBlockingWorldInput()) {
    if (const Spell *spell = ui.GetSelectedSpell()) {
      Vector2 origin = m_turn.LocalPreview().end.Center();
      Vector2 mouse = GetMousePosition();
      Vector2 aim{mouse.x / CELL_SIZE - origin.x,
                  mouse.y / CELL_SIZE - origin.y};
      float len = std::hypot(aim.x, aim.y);
      if (len > 0.001f)
        ui.DrawAimIndicator(*spell, origin, {aim.x / len, aim.y / len},
                            WorldGravity(sim));
    }
  }

  DrawHud();
  DrawBanner();
}

void Game::DrawSlotPlan(const Slot &slot, const PlanPreview &preview,
                        const Simulation &sim, const UI &ui) const {
  // Path as a thin polyline
  Color pathColor = WithAlpha(slot.color, 150);
  Vector2 prev = ToScreen(slot.character.Center());
  for (Vector2 p : preview.path) {
    Vector2 cur = ToScreen(p);
    DrawLineEx(prev, cur, 1.5f, pathColor);
    prev = cur;
  }

  // Each queued cast: the beam it will fire from where it fires
  for (const auto &mark : preview.casts) {
    ui.DrawSpellBeam(mark.stats, mark.origin, mark.direction,
                     WithAlpha(ui.GetSpellColor(mark.spell), 200),
                     WorldGravity(sim));
    DrawCircleV(ToScreen(mark.origin), 3.0f, slot.color);
  }

  // Where the player will be when the turn's plan runs out
  if (!preview.path.empty())
    DrawCharacter(preview.end, WithAlpha(slot.color, 170), false);
}

void Game::DrawPendingCasts(const Simulation &sim, const UI &ui) const {
  // Casts queued during this pause fire from the ghost's current spot
  Vector2 origin = m_turn.LocalPreview().end.Center();
  for (const PlannedCast &cast : m_turn.PendingCasts()) {
    SpellStats stats = SpellSystem::Evaluate(cast.spell);
    Vector2 dir = SpellSystem::ResolveDirection(stats, cast.aim);
    ui.DrawSpellBeam(stats, origin, dir,
                     WithAlpha(ui.GetSpellColor(cast.spell), 230),
                     WorldGravity(sim));
  }
}

void Game::DrawCharacter(const Character &c, Color color, bool drawHp) const {
  Rectangle r = ToScreen(c.Bounds());
  DrawRectangleRec(r, color);
  DrawRectangleLinesEx(r, 1.0f, Color{20, 20, 30, color.a});

  // Eye to show facing
  float eyeX = r.x + r.width * (c.facing > 0 ? 0.7f : 0.3f);
  DrawCircleV({eyeX, r.y + r.height * 0.25f}, 1.5f, Color{20, 20, 30, color.a});

  // Flames wrapping the body, more of them the more stacks there are
  if (c.Burning()) {
    int frame = static_cast<int>(GetTime() * 14.0);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 3 + c.burnStacks * 2; ++i) {
      uint32_t h = (uint32_t)(i * 2654435761u) ^ (uint32_t)(frame * 40503u);
      h ^= h >> 15;
      float fx = r.x + (h % 100) / 100.0f * r.width;
      float fy = r.y + r.height * (0.2f + ((h >> 8) % 80) / 100.0f);
      float size = 2.0f + ((h >> 16) % 4);
      DrawTriangle({fx, fy - size * 2.2f}, {fx - size, fy}, {fx + size, fy},
                   Color{255, (unsigned char)(90 + (h >> 20) % 120), 20,
                         (unsigned char)(150 * color.a / 255)});
    }
    EndBlendMode();
  }

  if (!drawHp)
    return;
  float w = 28.0f;
  Vector2 top{r.x + r.width * 0.5f - w * 0.5f, r.y - 8.0f};
  DrawRectangleV(top, {w, 4.0f}, Color{40, 20, 20, 220});
  DrawRectangleV(top, {w * (c.hp / c.maxHp), 4.0f}, Color{90, 220, 110, 255});
}

void Game::DrawHud() const {
  constexpr float barW = 360.0f;
  constexpr float barH = 14.0f;
  float x = (WINDOW_WIDTH - barW) * 0.5f;
  float y = 12.0f;

  bool planning = m_turn.GetPhase() == TurnController::Phase::Planning;
  const char *phase = m_waiting    ? "READY"
                      : !planning   ? "EXECUTING"
                      : m_paused    ? "PLANNING - TIME STOPPED"
                                    : "PLANNING - TIME FLOWING";
  DrawText(TextFormat("ROUND %d   TURN %d  -  %s", m_round, m_turnNumber,
                      phase),
           (int)x, (int)y, 16, RAYWHITE);
  y += 20.0f;

  DrawRectangleV({x, y}, {barW, barH}, Color{30, 30, 45, 230});
  float perTick = barW / TurnController::TURN_TICKS;

  // Movement in blue, channelling in amber (empty until time is stopped)
  static const std::vector<PlanStep> kNoSteps;
  const auto &steps = m_waiting ? kNoSteps : m_turn.LocalPlan().steps;
  int owed = 0;
  for (size_t i = 0; i < steps.size(); ++i) {
    for (const PlannedCast &cast : steps[i].casts)
      owed += TurnController::CastTicks(cast.spell);
    bool channelling = owed > 0;
    owed = std::max(0, owed - 1);
    Color c = channelling ? Color{255, 190, 70, 255} : Color{110, 160, 255, 255};
    DrawRectangleV({x + i * perTick, y}, {perTick + 0.5f, barH}, c);
  }
  // Channel time already promised to casts queued in this pause
  if (planning && !m_waiting) {
    int reserved = TurnController::TURN_TICKS - m_turn.TicksFree() -
                   m_turn.TicksUsed();
    if (reserved > 0)
      DrawRectangleV({x + steps.size() * perTick, y},
                     {reserved * perTick, barH}, Color{255, 190, 70, 110});
  }
  if (!planning && !m_waiting) {
    float px = x + m_turn.ExecutedTicks() * perTick;
    DrawLineEx({px, y - 2}, {px, y + barH + 2}, 2.0f, RAYWHITE);
  }
  DrawRectangleLinesEx({x, y, barW, barH}, 1.0f, Color{200, 200, 220, 255});
  DrawText(TextFormat("%.2fs / %.1fs",
                      steps.size() * TurnController::TICK_DT,
                      TurnController::TURN_SECONDS),
           (int)(x + barW + 8), (int)y, 14, RAYWHITE);
  y += barH + 6.0f;

  if (m_state == RoundState::Playing && m_waiting)
    DrawText("Space stop time  R reset opponent  F1 sandbox", (int)x, (int)y,
             12, Color{190, 190, 210, 255});
  else if (m_state == RoundState::Playing && planning)
    DrawText(m_paused
                 ? "Click queue cast  Space let time flow  Backspace undo  "
                   "Enter run now"
                 : "A/D move  W jump  Click cast  Space stop time  "
                   "Backspace undo  (runs when the bar fills)",
             (int)x - 40, (int)y, 12, Color{190, 190, 210, 255});

  if (m_notice && m_noticeTime > 0.0f)
    DrawText(m_notice, (int)x, (int)y + 18, 16, Color{255, 170, 90, 255});
}

void Game::DrawBanner() const {
  bool defeated = m_state == RoundState::Defeated;
  if (!m_banner || (!defeated && m_bannerTime <= 0.0f))
    return;

  Color color = defeated ? Color{230, 80, 80, 255} : Color{120, 230, 140, 255};
  constexpr int size = 64;
  int w = MeasureText(m_banner, size);
  int y = WINDOW_HEIGHT / 2 - 90;
  DrawRectangle(0, y - 16, WINDOW_WIDTH, size + 60, Color{10, 10, 15, 190});
  DrawText(m_banner, (WINDOW_WIDTH - w) / 2, y, size, color);

  const char *sub = defeated ? "Press Enter to restart the arena"
                             : TextFormat("Round %d", m_round);
  int sw = MeasureText(sub, 20);
  DrawText(sub, (WINDOW_WIDTH - sw) / 2, y + size + 8, 20, RAYWHITE);
}
