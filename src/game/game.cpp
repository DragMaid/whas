#include "whas/game/game.h"
#include "whas/audio/audio_manager.h"
#include "whas/game/character_draw.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/engine/view.h"
#include "whas/net/lockstep_client.h"
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

void Game::StartOnline(Simulation &sim, UI &ui, LockstepClient &client) {
  m_online = &client;
  m_active = true;
  m_arenaReady = true;
  ui.SetGameMode(true);
  // Online matches play by the default rules on both machines
  sim.GetConfig() = SimulationConfig{};
  m_local = std::max(0, client.Slot());
  m_slots[Local()].color = LOCAL_COLOR;
  m_slots[Opponent()].color = OPPONENT_COLOR;
  m_roundsWon = {};
  m_seenRoundEnds = client.RoundEnds();
  m_plannedTurn = -1;
  m_submitted = false;
  m_state = RoundState::Playing;
  m_banner = nullptr;
  m_turnNumber = 1;
  EnterWaiting();
}

void Game::LeaveOnline(UI &ui) {
  m_online = nullptr;
  m_active = false;
  m_arenaReady = false;
  ui.SetGameMode(false);
  ui.ClearMatchSpells();
}

PlannedCast Game::MakeCast(const Spell &spell, Vector2 aim) const {
  PlannedCast cast = PlannedCast::Local(spell, aim);
  if (!m_online)
    return cast;
  // Online the server's card is what counts: its id and its stats
  for (const auto &card : m_online->Cards(Local()))
    if (card && card->spell.name == spell.name &&
        card->spell.glyphs.size() == spell.glyphs.size() &&
        card->spell.components.size() == spell.components.size()) {
      cast.spell = card->spell;
      cast.stats = card->stats;
      cast.spellId = card->id;
      return cast;
    }
  cast.stats.valid = false;
  return cast;
}

void Game::UpdateOnline(Simulation &sim, UI &ui) {
  using Phase = LockstepClient::Phase;
  LockstepClient &client = *m_online;
  client.Update(sim, m_match);
  for (std::string &notice : client.TakeNotices()) {
    m_noticeText = std::move(notice);
    Notify(m_noticeText.c_str(), 4.0f);
  }
  m_local = std::max(0, client.Slot());

  std::array<std::optional<Spell>, DECK_SLOTS> spells;
  for (int i = 0; i < DECK_SLOTS; ++i)
    if (const auto &card = client.Cards(Local())[i])
      spells[i] = card->spell;
  ui.SetMatchSpells(spells);

  // Round results
  if (client.RoundEnds() != m_seenRoundEnds) {
    m_seenRoundEnds = client.RoundEnds();
    m_roundsWon = client.RoundsWon();
    int winner = client.LastRoundWinner();
    m_banner = winner >= Match::PLAYERS ? "DRAW ROUND"
               : winner == Local()     ? "ROUND WON"
                                       : "ROUND LOST";
    m_bannerSub = nullptr;
    m_bannerTime = ROUND_BANNER_SECONDS;
  }

  switch (client.GetPhase()) {
  case Phase::Planning: {
    int key = client.Round() * 1000 + client.Turn();
    if (key != m_plannedTurn) {
      // A new turn: time stops and planning starts at once
      m_plannedTurn = key;
      m_turnNumber = client.Turn() + 1;
      m_submitted = false;
      m_waiting = false;
      m_paused = true;
      m_turn.BeginPlanning(LocalCharacter());
      m_slots[Opponent()].plan = {};
      m_slots[Opponent()].preview = {};
    }
    if (!m_submitted) {
      UpdatePlanning(sim, ui);
      // Don't let the server's deadline pass: send what there is
      if (!m_submitted && client.SecondsLeft() < 1.0f) {
        m_turn.Flush(sim);
        Commit();
      }
    }
    break;
  }
  case Phase::Executing:
    m_waiting = false;
    if (m_turn.GetPhase() != TurnController::Phase::Executing)
      m_turn.BeginExecution();
    m_slots[0].plan = client.Plans()[0];
    m_slots[1].plan = client.Plans()[1];
    client.StepExecution(sim, m_match, 1);
    break;
  case Phase::MatchOver:
    if (m_state != RoundState::MatchOver) {
      m_state = RoundState::MatchOver;
      m_roundsWon = client.RoundsWon();
      int w = client.MatchWinner();
      m_banner = w < 0 ? "DRAW" : w == Local() ? "VICTORY" : "DEFEATED";
      m_bannerSub = "Press Enter to leave";
    }
    if (IsKeyPressed(KEY_ENTER))
      m_exitRequested = true;
    break;
  case Phase::Offline:
    if (IsKeyPressed(KEY_ENTER))
      m_exitRequested = true;
    break;
  default:
    m_waiting = true;
    break;
  }
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
  if (m_online) {
    m_online->SubmitPlan(m_turn.LocalPlan());
    m_submitted = true;
    m_waiting = true; // until both plans are in
    return;
  }
  m_turn.BeginExecution();
}

void Game::ToggleTime(Simulation &sim) {
  if (m_state != RoundState::Playing)
    return;
  // Online, turns start when the server says so
  if (m_online && (m_waiting || m_submitted))
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
  if (m_state == RoundState::MatchOver)
    return ClockState::Over;
  if (m_online) {
    using Phase = LockstepClient::Phase;
    Phase p = m_online->GetPhase();
    if (p == Phase::Executing)
      return ClockState::Executing;
    if (p == Phase::Planning && !m_submitted)
      return m_paused ? ClockState::Stopped : ClockState::Running;
    return ClockState::Waiting;
  }
  if (m_state != RoundState::Playing)
    return ClockState::Over;
  if (m_waiting)
    return ClockState::Waiting;
  if (m_turn.GetPhase() == TurnController::Phase::Executing)
    return ClockState::Executing;
  return m_paused ? ClockState::Stopped : ClockState::Running;
}

float Game::TurnProgress() const {
  if (m_online && m_online->GetPhase() == LockstepClient::Phase::Executing)
    return static_cast<float>(m_online->ExecutedTicks()) /
           TurnController::TURN_TICKS;
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

void Game::Update(Simulation &sim, UI &ui, UIState &state) {
  if (state.timeToggleRequested)
    ToggleTime(sim);
  state.timeToggleRequested = false;
  state.resetAvatarRequested = false;

  Update(sim, ui);
  m_trail.Update(m_match.characters.data(), Match::PLAYERS, GetFrameTime());

  switch (GetClockState()) {
  case ClockState::Waiting:
    state.clock = ClockLook::Waiting;
    break;
  case ClockState::Running:
    state.clock = ClockLook::Running;
    break;
  case ClockState::Stopped:
    state.clock = ClockLook::Stopped;
    break;
  case ClockState::Executing:
    state.clock = ClockLook::Executing;
    break;
  case ClockState::Over:
    state.clock = ClockLook::Over;
    break;
  }
  state.clockProgress = TurnProgress();
  state.ticksFree = TicksFree();
  state.matchRound = m_match.round;

  // Flashed during the turn: blind for the rest of it, then the white
  // fades through the next planning phase
  bool executing = GetClockState() == ClockState::Executing;
  ui.Blind(LocalCharacter().TakeFlash(), executing);
}

void Game::Update(Simulation &sim, UI &ui) {
  float frame = GetFrameTime();
  m_noticeTime = std::max(0.0f, m_noticeTime - frame);
  m_bannerTime = std::max(0.0f, m_bannerTime - frame);

  if (m_online) {
    UpdateOnline(sim, ui);
    return;
  }
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

  if (!m_online && IsKeyPressed(KEY_R)) {
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
      // Notify keeps the pointer: hold the text in the game
      m_noticeText = SpellSystem::Problem(*spell);
      Notify(m_noticeText.c_str(), 3.0f);
    } else {
      Vector2 origin = ghost.Center();
      Vector2 mouse = View::MouseCells();
      Vector2 aim{mouse.x - origin.x, mouse.y - origin.y};
      float len = std::hypot(aim.x, aim.y);
      aim = len > 0.001f ? Vector2{aim.x / len, aim.y / len}
                         : Vector2{(float)ghost.facing, 0.0f};
      switch (m_turn.QueueCast(MakeCast(*spell, aim))) {
      case TurnController::CastResult::Queued:
        if (AudioManager *audio = AudioManager::Instance())
          audio->PlayUi(UiSound::SpellPlan);
        break;
      case TurnController::CastResult::NoTime:
        Notify("Not enough time left in this turn to cast that", 2.0f);
        break;
      case TurnController::CastResult::SecondFlight:
        Notify("Only one wind underfoot (movement) cast per pause", 2.0f);
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
    // The cursor is recorded too: sights set spells follow it
    m_turn.FlowTick(sim, input, PlanCursor::FromCells(View::MouseCells()));
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

  m_trail.Draw();
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
      Vector2 mouse = View::MouseCells();
      Vector2 aim{mouse.x - origin.x, mouse.y - origin.y};
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
  DrawCharacterBody(c, color, drawHp);
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
  if (m_online) {
    using Phase = LockstepClient::Phase;
    switch (m_online->GetPhase()) {
    case Phase::Planning:
      phase = m_submitted ? "PLAN SENT - WAITING FOR OPPONENT"
                          : TextFormat("%s - %.0fs LEFT",
                                       m_paused ? "TIME STOPPED" : "TIME FLOWING",
                                       std::ceil(m_online->SecondsLeft()));
      break;
    case Phase::Committed:
    case Phase::Revealed:
      phase = m_online->OpponentCommitted() ? "BOTH READY"
                                            : "PLAN SENT - WAITING FOR OPPONENT";
      break;
    case Phase::Executing:
      phase = "EXECUTING";
      break;
    case Phase::Decks:
      phase = "LOCKING DECKS";
      break;
    case Phase::Resync:
      phase = "RESYNCING";
      break;
    case Phase::Offline:
      phase = "DISCONNECTED - Enter to leave";
      break;
    case Phase::MatchOver:
      phase = "MATCH OVER";
      break;
    default:
      phase = "WAITING FOR THE SERVER";
      break;
    }
    planning = m_online->GetPhase() == Phase::Planning && !m_submitted;
  }
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
    int executed = m_online ? m_online->ExecutedTicks() : m_turn.ExecutedTicks();
    float px = x + executed * perTick;
    DrawLineEx({px, y - 2}, {px, y + barH + 2}, 2.0f, RAYWHITE);
  }
  DrawRectangleLinesEx({x, y, barW, barH}, 1.0f, Color{200, 200, 220, 255});
  DrawText(TextFormat("%.2fs / %.1fs",
                      steps.size() * TurnController::TICK_DT,
                      TurnController::TURN_SECONDS),
           (int)(x + barW + 8), (int)y, 14, RAYWHITE);
  y += barH + 6.0f;

  if (m_online) {
    if (!m_online->OpponentConnected())
      DrawText("Opponent disconnected - they have a minute to come back",
               (int)x, (int)y, 12, Color{255, 170, 90, 255});
    else if (planning)
      DrawText("Click cast  Space stop/flow time  Backspace undo  Enter send "
               "plan",
               (int)x - 20, (int)y, 12, Color{190, 190, 210, 255});
  } else if (m_state == RoundState::Playing && m_waiting)
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
