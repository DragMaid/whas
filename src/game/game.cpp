#include "whas/game/game.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/spell/spell_system.h"
#include "whas/ui/ui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string_view>

namespace {

constexpr Color LOCAL_COLOR{230, 230, 240, 255};
constexpr Color OPPONENT_COLOR{220, 80, 80, 255};
constexpr float ROUND_BANNER_SECONDS = 2.5f;

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

// Offline matches get a fresh arena every time
uint64_t OfflineSeed() {
  return static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

void Game::SetActive(bool active, Simulation &sim, UI &ui) {
  m_active = active;
  ui.SetGameMode(active);
  if (!active)
    return;

  if (!m_arenaReady)
    StartMatch(sim, OfflineSeed());
  EnterWaiting();
}

void Game::StartMatch(Simulation &sim, uint64_t seed, int localSlot) {
  m_local = localSlot;
  m_slots[Local()].color = LOCAL_COLOR;
  m_slots[Opponent()].color = OPPONENT_COLOR;
  m_roundsWon = {};
  m_match.seed = seed;
  BeginRound(sim, 0);
  m_arenaReady = true;
}

void Game::BeginRound(Simulation &sim, int round) {
  m_match = Match::BeginRound(sim, m_match.seed, round);
  for (int i = 0; i < Match::PLAYERS; ++i)
    m_spawns[i] = m_match.characters[i].pos;
  m_turnNumber = 1;
  m_state = RoundState::Playing;
  EnterWaiting();
}

// Offline practice: put the dummy back where it started, at full health
void Game::ResetOpponent(Simulation &sim) {
  Character &c = m_match.characters[Opponent()];
  c.pos = m_spawns[Opponent()];
  c.vel = {0.0f, 0.0f};
  c.pushX = 0.0f;
  c.hp = c.maxHp;
  c.burnStacks = 0;
  c.burnExposure = 0;
  c.Step(sim, {}, 0.0f);
}

void Game::BeginPlanning(Simulation &sim) {
  m_turn.BeginPlanning(LocalCharacter());

  // The opponent's plan will arrive over the network; for now it's empty
  Slot &opponent = m_slots[Opponent()];
  opponent.plan = {};
  opponent.preview =
      PreviewPlan(sim, m_match.characters[Opponent()], opponent.plan,
                  TurnController::TICK_DT);
}

void Game::EnterWaiting() { m_waiting = true; }

void Game::Commit() {
  m_slots[Local()].plan = m_turn.LocalPlan();
  m_turn.BeginExecution();
}

void Game::ToggleTime(Simulation &sim) {
  if (m_state != RoundState::Playing)
    return;
  if (m_waiting) {
    // Stop time: start planning this turn
    m_waiting = false;
    m_paused = true;
    BeginPlanning(sim);
    return;
  }
  if (m_turn.GetPhase() != TurnController::Phase::Planning)
    return;
  m_paused = !m_paused;
  if (m_paused)
    m_turn.MarkPause();
}

Game::ClockState Game::GetClockState() const {
  if (m_state != RoundState::Playing)
    return ClockState::Over;
  if (m_waiting)
    return ClockState::Waiting;
  if (m_turn.GetPhase() == TurnController::Phase::Executing)
    return ClockState::Executing;
  return m_paused ? ClockState::Stopped : ClockState::Running;
}

float Game::TurnProgress() const {
  if (m_waiting)
    return 0.0f;
  int ticks = m_turn.GetPhase() == TurnController::Phase::Executing
                  ? m_turn.ExecutedTicks()
                  : TurnController::TURN_TICKS - m_turn.TicksFree();
  return static_cast<float>(ticks) / TurnController::TURN_TICKS;
}

int Game::TicksFree() const {
  if (m_waiting)
    return TurnController::TURN_TICKS;
  return m_turn.TicksFree();
}

void Game::UpdateWaiting(Simulation &sim) {
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;

  if (IsKeyPressed(KEY_R)) {
    ResetOpponent(sim);
    Notify("Opponent reset", 1.5f);
  }

  if (IsKeyPressed(KEY_SPACE))
    ToggleTime(sim);
}

void Game::Notify(const char *text, float seconds) {
  m_notice = text;
  m_noticeTime = seconds;
}

void Game::Update(Simulation &sim, UI &ui) {
  float frame = GetFrameTime();
  m_noticeTime = std::max(0.0f, m_noticeTime - frame);
  m_bannerTime = std::max(0.0f, m_bannerTime - frame);

  if (m_state == RoundState::MatchOver) {
    if (IsKeyPressed(KEY_ENTER))
      StartMatch(sim, OfflineSeed(), m_local);
    return;
  }
  if (m_state == RoundState::RoundOver) {
    if (m_bannerTime <= 0.0f)
      BeginRound(sim, m_match.round + 1);
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
    ResetOpponent(sim);
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
  if (IsKeyPressed(KEY_SPACE))
    ToggleTime(sim);

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ui.IsBlockingWorldInput()) {
    const Spell *spell = ui.GetSelectedSpell();
    const Character &ghost = m_turn.LocalPreview().end;
    if (!spell) {
      Notify("Pick a spell from the hotbar first (1-6)", 2.0f);
    } else if (!SpellSystem::Evaluate(*spell).valid) {
      Notify("That spell needs exactly one known sigil", 2.0f);
    } else {
      Vector2 origin = ghost.Center();
      Vector2 mouse = GetMousePosition();
      Vector2 aim{mouse.x / CELL_SIZE - origin.x, mouse.y / CELL_SIZE - origin.y};
      float len = std::hypot(aim.x, aim.y);
      aim = len > 0.001f ? Vector2{aim.x / len, aim.y / len}
                         : Vector2{(float)ghost.facing, 0.0f};
      switch (m_turn.QueueCast(PlannedCast::Local(*spell, aim))) {
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
  Match::ExecuteTick(sim, m_match, {&m_slots[0].plan, &m_slots[1].plan},
                     m_turn.ExecutedTicks());
  if (!m_turn.Advance())
    FinishTurn(sim);
}

void Game::FinishTurn(Simulation &sim) {
  m_turnNumber++;
  Match::EndTurn(m_match);

  int winner = Match::RoundWinner(m_match);
  if (winner < 0) {
    EnterWaiting();
    return;
  }

  if (winner < Match::PLAYERS)
    m_roundsWon[winner]++;
  int needed = Match::ROUNDS / 2 + 1;
  bool over = m_roundsWon[0] >= needed || m_roundsWon[1] >= needed ||
              m_match.round + 1 >= Match::ROUNDS;
  bool won = winner == Local();
  if (over) {
    m_state = RoundState::MatchOver;
    bool matchWon = m_roundsWon[Local()] > m_roundsWon[Opponent()];
    bool draw = m_roundsWon[Local()] == m_roundsWon[Opponent()];
    m_banner = draw ? "DRAW" : matchWon ? "VICTORY" : "DEFEATED";
    m_bannerSub = "Press Enter for a new match";
    m_bannerTime = 0.0f; // stays up until Enter
    return;
  }
  m_state = RoundState::RoundOver;
  m_banner = winner == Match::PLAYERS ? "DOUBLE KO"
             : won                    ? "ROUND WON"
                                      : "ROUND LOST";
  m_bannerSub = nullptr;
  m_bannerTime = ROUND_BANNER_SECONDS;
}

void Game::Draw(const Simulation &sim, const UI &ui) const {
  bool planning = m_state == RoundState::Playing && !m_waiting &&
                  m_turn.GetPhase() == TurnController::Phase::Planning;

  for (int i = 0; i < (int)m_slots.size(); ++i) {
    const Slot &slot = m_slots[i];
    const Character &c = m_match.characters[i];
    Color body = c.Alive() ? slot.color : GRAY;
    // While planning, the real body is where the turn starts: draw it dimmed
    DrawCharacter(c, planning ? WithAlpha(body, 110) : body, true);

    if (!planning)
      continue;
    if (i == Local()) {
      DrawSlotPlan(i, m_turn.LocalPreview(), sim, ui);
      DrawPendingCasts(sim, ui);
    } else {
      DrawSlotPlan(i, slot.preview, sim, ui);
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

void Game::DrawSlotPlan(int slotIndex, const PlanPreview &preview,
                        const Simulation &sim, const UI &ui) const {
  const Slot &slot = m_slots[slotIndex];
  // Path as a thin polyline
  Color pathColor = WithAlpha(slot.color, 150);
  Vector2 prev = ToScreen(m_match.characters[slotIndex].Center());
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
    Vector2 dir = SpellSystem::ResolveDirection(cast.stats, cast.aim);
    ui.DrawSpellBeam(cast.stats, origin, dir,
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
  DrawText(TextFormat("ROUND %d (%d-%d)   TURN %d  -  %s", m_match.round + 1,
                      m_roundsWon[Local()], m_roundsWon[Opponent()],
                      m_turnNumber, phase),
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
      owed += TurnController::CastTicks(cast.stats);
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
    DrawText("Space stop time  R reset dummy  F1 sandbox", (int)x, (int)y,
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
  bool sticky = m_state == RoundState::MatchOver;
  if (!m_banner || (!sticky && m_bannerTime <= 0.0f))
    return;

  bool bad = m_banner[0] == 'D' || std::string_view(m_banner) == "ROUND LOST";
  Color color = bad ? Color{230, 80, 80, 255} : Color{120, 230, 140, 255};
  constexpr int size = 64;
  int w = MeasureText(m_banner, size);
  int y = WINDOW_HEIGHT / 2 - 90;
  DrawRectangle(0, y - 16, WINDOW_WIDTH, size + 60, Color{10, 10, 15, 190});
  DrawText(m_banner, (WINDOW_WIDTH - w) / 2, y, size, color);

  const char *sub =
      m_bannerSub ? m_bannerSub
                  : TextFormat("Round %d of %d  (%d - %d)", m_match.round + 2,
                               Match::ROUNDS, m_roundsWon[Local()],
                               m_roundsWon[Opponent()]);
  int sw = MeasureText(sub, 20);
  DrawText(sub, (WINDOW_WIDTH - sw) / 2, y + size + 8, 20, RAYWHITE);
}
