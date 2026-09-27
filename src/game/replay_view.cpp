#include "whas/game/replay_view.h"
#include "imgui.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include "whas/game/character_draw.h"
#include "whas/ui/ui.h"

namespace {
constexpr Color SLOT_COLORS[2] = {{230, 230, 240, 255}, {220, 80, 80, 255}};
}

bool ReplayView::Open(const nlohmann::json &replay, Simulation &sim) {
  m_error.clear();
  if (!m_player.Load(replay, m_error)) {
    m_active = false;
    return false;
  }
  m_player.Start(sim);
  m_active = true;
  m_playing = true;
  m_speed = 1;
  return true;
}

void ReplayView::Update(Simulation &sim, UIState &state) {
  state.matchRound = -1;
  state.clock = m_playing ? ClockLook::Executing : ClockLook::Stopped;
  state.clockProgress =
      static_cast<float>(m_player.Tick()) / TurnController::TURN_TICKS;
  state.timeToggleRequested = false;
  if (!ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(KEY_SPACE))
    m_playing = !m_playing;
  if (m_playing && !m_player.Finished())
    m_player.Step(sim, m_speed);
}

void ReplayView::Draw() const {
  for (int i = 0; i < 2; ++i) {
    const Character &c = m_player.State().characters[i];
    DrawCharacterBody(c, c.Alive() ? SLOT_COLORS[i] : GRAY, true);
  }
}

void ReplayView::DrawControls(Simulation &sim) {
  ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Appearing);
  ImGui::Begin("Replay", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  ImGui::Text("Match #%lld", (long long)m_player.MatchId());
  if (m_player.Finished())
    ImGui::Text("Finished (%d turns)", m_player.TurnCount());
  else
    ImGui::Text("Round %d  turn %d  (%d / %d)", m_player.Round() + 1,
                m_player.Turn() + 1, m_player.CurrentTurn() + 1,
                m_player.TurnCount());

  if (ImGui::Button(m_playing ? "Pause" : "Play", {70, 0}))
    m_playing = !m_playing;
  ImGui::SameLine();
  if (ImGui::Button("Next turn")) {
    int left = TurnController::TURN_TICKS - m_player.Tick();
    m_player.Step(sim, left);
  }
  ImGui::SameLine();
  if (ImGui::Button("Restart"))
    m_player.Start(sim);
  ImGui::SameLine();
  if (ImGui::Button("Close"))
    m_active = false;

  ImGui::Text("Speed");
  for (int s : {1, 2, 4, 8}) {
    ImGui::SameLine();
    if (ImGui::RadioButton(TextFormat("%dx", s), m_speed == s))
      m_speed = s;
  }

  if (m_player.Checked() > 0) {
    if (m_player.Mismatches() == 0)
      ImGui::TextColored({0.5f, 0.9f, 0.5f, 1}, "%d hashes match the players'",
                         m_player.Checked());
    else
      ImGui::TextColored({1, 0.5f, 0.4f, 1},
                         "%d of %d hashes differ (a resync or another build)",
                         m_player.Mismatches(), m_player.Checked());
  }
  ImGui::End();
}
