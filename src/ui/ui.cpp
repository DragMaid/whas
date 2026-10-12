#include "whas/ui/ui.h"
#include "whas/engine/view.h"
#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "whas/constants.h"
#include "whas/game/character.h"
#include "whas/ui/audio_settings.h"
#include "whas/ui/theme.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using Theme::Tone;

namespace {

void DrawDashedLine(Vector2 a, Vector2 b, float dash, float thick,
                    Color color) {
  float len = std::hypot(b.x - a.x, b.y - a.y);
  if (len < 0.001f)
    return;
  Vector2 dir{(b.x - a.x) / len, (b.y - a.y) / len};
  for (float t = 0.0f; t < len; t += dash * 2.0f) {
    float e = std::min(t + dash, len);
    DrawLineEx({a.x + dir.x * t, a.y + dir.y * t},
               {a.x + dir.x * e, a.y + dir.y * e}, thick, color);
  }
}

void DrawArrow(Vector2 from, Vector2 dir, float length, float thick,
               Color color) {
  Vector2 tip{from.x + dir.x * length, from.y + dir.y * length};
  DrawLineEx(from, tip, thick, color);
  float head = std::min(12.0f, length * 0.35f);
  Vector2 back{-dir.x * head, -dir.y * head};
  Vector2 side{-dir.y * head * 0.5f, dir.x * head * 0.5f};
  DrawTriangle(tip, {tip.x + back.x - side.x, tip.y + back.y - side.y},
               {tip.x + back.x + side.x, tip.y + back.y + side.y}, color);
  DrawTriangle(tip, {tip.x + back.x + side.x, tip.y + back.y + side.y},
               {tip.x + back.x - side.x, tip.y + back.y - side.y}, color);
}

// Matches the gravity scale in ParticleSystem::Update
constexpr float kParticleGravity = 20.0f;

} // namespace

UI::UI() {
  rlImGuiSetLoadFontsCallback(Theme::LoadImGuiFonts);
  rlImGuiSetup(true);
  Theme::Apply(ImGui::GetStyle());
  Theme::LoadRaylibFonts();
  m_baseStyle = ImGui::GetStyle();
  m_library.Load();
  std::vector<std::string> starter;
  for (const Spell &spell : m_library.All())
    starter.push_back(m_library.RefOf(spell));
  m_decks.Load(starter);
  m_spellEditor.Bind(&m_library, &m_decks);
  m_thumbnails = std::make_unique<SpellThumbnails>(m_spellEditor.Glyphs());
  m_spellEditor.SetThumbnails(m_thumbnails.get());
}

UI::~UI() {
  m_thumbnails.reset(); // textures before the GL context goes
  Theme::UnloadRaylibFonts();
  rlImGuiShutdown();
}

void UI::HandleInput(UIState &state, Simulation &sim) {
  (void)sim;
  if (m_spellEditor.IsOpen())
    return;
  if (ImGui::GetIO().WantCaptureKeyboard)
    return;

  if (IsKeyPressed(KEY_F3))
    state.debugOverlay = !state.debugOverlay;
  if (IsKeyPressed(KEY_F4))
    state.showConfigEditor = !state.showConfigEditor;
  if (state.keysTaken)
    return;
  if (IsKeyPressed(KEY_E))
    m_spellEditor.Open();
  if (IsKeyPressed(KEY_M))
    state.menuRequested = true;

  if (!m_gameMode && IsKeyPressed(KEY_TAB))
    state.tool = state.tool == SandboxTool::Draw ? SandboxTool::Cast
                                                 : SandboxTool::Draw;

  bool drawing = !m_gameMode && state.tool == SandboxTool::Draw;
  if (drawing) {
    static constexpr Element kKeys[] = {
        Element::WATER, Element::EARTH, Element::FIRE,
        Element::STEAM, Element::CLOUD, Element::ICE,
        Element::SAND,  Element::ROCK,  Element::AIR};
    for (int i = 0; i < 9; ++i)
      if (IsKeyPressed(KEY_ONE + i))
        state.selectedMaterial = kKeys[i];
    if (!ImGui::GetIO().WantCaptureMouse) {
      float wheel = GetMouseWheelMove();
      if (wheel != 0)
        state.brushRadius =
            std::clamp(state.brushRadius + (int)wheel, 1, 20);
    }
  } else {
    for (int i = 0; i < DECK_SLOTS; ++i)
      if (IsKeyPressed(KEY_ONE + i))
        SelectSlot(i);
    if (!ImGui::GetIO().WantCaptureMouse) {
      float wheel = GetMouseWheelMove();
      if (wheel != 0)
        SelectSlot((m_selectedSlot - (int)wheel + DECK_SLOTS) % DECK_SLOTS);
    }
  }
}

void UI::Blind(float seconds) {
  if (seconds <= m_blind)
    return;
  m_blind = seconds;
  m_blindTotal = seconds;
}

void UI::DrawBlindness() {
  if (m_blind <= 0.0f)
    return;
  m_blind = std::max(0.0f, m_blind - GetFrameTime());
  // A white glare that never quite hides the world and clears steadily
  float alpha = 0.9f * std::sqrt(m_blind / m_blindTotal);
  DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                Color{255, 255, 248, static_cast<unsigned char>(alpha * 255)});
}

void UI::SelectSlot(int slot) {
  m_selectedSlot = std::clamp(slot, 0, DECK_SLOTS - 1);
}

void UI::DrawWorld(const UIState &state, Simulation &sim) {
  if (state.debugOverlay)
    sim.GetRigidBodySystem().DrawDebug();
  DrawActiveFields(sim);
  DrawActiveColumns(sim);
}

void UI::ApplyUiScale() {
  float scale = View::UiScale();
  if (scale == m_uiScale)
    return;
  m_uiScale = scale;
  ImGuiStyle &style = ImGui::GetStyle();
  style = m_baseStyle;
  style.ScaleAllSizes(scale);
  style.FontScaleMain = scale;
}

void UI::Draw(UIState &state, Simulation &sim) {
  m_lastState = &state;
  DrawBlindness();

  ApplyUiScale();
  rlImGuiBegin();

  if (state.showConfigEditor && !state.configLocked && !m_spellEditor.IsOpen()) {
    DrawPropertyEditor(sim.GetConfig());
  }

  if (!m_spellEditor.IsOpen() && !state.hideActionBar) {
    DrawActionBar(state);
    if (state.debugOverlay)
      DrawInspector(sim);
  }

  if (m_overlay && !m_spellEditor.IsOpen())
    m_overlay();
  m_spellEditor.Draw();
  HandleEditorRequests(state);

  rlImGuiEnd();
}

void UI::HandleEditorRequests(UIState &state) {
  std::string ref;
  if (m_spellEditor.TakeTestRequest(ref)) {
    m_testSpellRef = ref;
    m_selectedSlot = 0;
    state.tool = SandboxTool::Cast;
    state.sandboxRequested = true;
    m_spellEditor.Close();
  }
}

void UI::DrawInspector(Simulation &sim) {
  if (IsMouseOverPanel())
    return;

  Vector2 cellPos = GetMouseCell();
  int cx = (int)cellPos.x;
  int cy = (int)cellPos.y;

  if (cx < 0 || cx >= GRID_W || cy < 0 || cy >= GRID_H)
    return;

  const Cell &cell = sim.GetCell(cx, cy);

  Vector2 mousePos = GetMousePosition();
  ImVec2 pivot = ImVec2(-0.1f, 1.1f);

  if (mousePos.y < 160)
    pivot.y = -0.1f;
  if (mousePos.x > GetScreenWidth() - 200)
    pivot.x = 1.1f;

  ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always, pivot);
  ImGui::Begin(
      "Inspector", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNav);

  ImGui::TextColored(Theme::Vec(Tone::Brass), "Cell [%d, %d]", cx, cy);
  ImGui::Separator();
  ImGui::Text("Type: %s", ElementName(cell.element));
  ImGui::Text("Temp: %.1f C", cell.temperature);
  ImGui::Text("Pressure: %.2f", cell.pressure);
  ImGui::Text("Velocity: (%.2f, %.2f)", cell.vx, cell.vy);
  ImGui::Text("Mass: %.2f g", cell.mass);
  ImGui::Text("Density: %.2f g/cm3", cell.density);
  if (cell.lifetime > 0)
    ImGui::Text("Lifetime: %.2f s", cell.lifetime);
  if (cell.moisture > 0)
    ImGui::Text("Moisture: %.2f", cell.moisture);

  ImGui::End();
}

void UI::DrawPropertyEditor(SimulationConfig &config) {
  ImGui::Begin("Simulation Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

  if (ImGui::CollapsingHeader("World")) {
    ImGui::SliderFloat("Gravity", &config.world.gravity, -1.0f, 1.0f);
    ImGui::SliderFloat("Pressure Eq", &config.world.pressureEq, 0.0f, 1.0f);
    ImGui::SliderFloat("Ambient Temp", &config.world.ambientTemp, -50.0f,
                       100.0f);
  }

  if (ImGui::CollapsingHeader("Fluid Physics")) {
    ImGui::SliderInt("Pressure Scan Depth", &config.fluid.pressureScanDepth, 1,
                     50);
    ImGui::SliderFloat("Pressure Weight", &config.fluid.pressureWeight, 0.0f,
                       2.0f);
    ImGui::SliderFloat("Gas Displacement Chance",
                       &config.fluid.gasDisplacementChance, 0.0f, 1.0f);

    if (ImGui::TreeNode("Water Specific")) {
      ImGui::SliderFloat("Density", &config.fluid.water.density, 0.1f, 10.0f);
      ImGui::SliderFloat("Viscosity", &config.fluid.water.viscosity, 0.0f,
                         1.0f);
      ImGui::SliderFloat("Max Fall Speed", &config.fluid.water.maxFallSpeed,
                         0.0f, 20.0f);
      ImGui::SliderFloat("Max Horizontal Speed",
                         &config.fluid.water.maxHorizontalSpeed, 0.0f, 20.0f);
      ImGui::SliderFloat("Spread Factor", &config.fluid.water.spreadFactor,
                         0.0f, 1.0f);
      ImGui::SliderFloat("Friction", &config.fluid.water.friction, 0.0f, 1.0f);
      ImGui::Checkbox("Can Displace Gas", &config.fluid.water.canDisplaceGas);
      ImGui::Checkbox("Can Erode Terrain", &config.fluid.water.canErodeTerrain);
      ImGui::TreePop();
    }
  }

  if (ImGui::CollapsingHeader("Element Properties")) {
    DrawElementPropertyEditor(config);
  }

  if (ImGui::CollapsingHeader("Element Specifics")) {
    if (ImGui::TreeNode("Cloud")) {
      ImGui::SliderFloat("Freezing Point", &config.cloud.freezingPoint, -20.0f,
                         20.0f);
      ImGui::SliderFloat("Min Moisture", &config.cloud.minMoisture, 0.0f, 1.0f);
      ImGui::SliderFloat("Wind Jitter", &config.cloud.windJitter, 0.0f, 0.5f);
      ImGui::SliderFloat("Max Drift", &config.cloud.maxDrift, 0.0f, 5.0f);
      ImGui::SliderInt("Rain Chance", &config.cloud.rainChance, 1, 500);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Fire")) {
      ImGui::SliderFloat("Min Temp", &config.fire.minTemp, 0.0f, 1000.0f);
      ImGui::SliderInt("Spark Chance", &config.fire.sparkChance, 1, 50);
      ImGui::TreePop();
    }
  }

  if (ImGui::CollapsingHeader("Audio"))
    DrawAudioSettings(true);

  ImGui::End();
}

void UI::DrawElementPropertyEditor(SimulationConfig &config) {
  const char *elementNames[] = {"AIR",   "WATER", "EARTH", "FIRE", "STEAM",
                                "CLOUD", "ICE",   "SAND",  "ROCK"};
  static int selectedElement = 0;

  ImGui::Combo("Select Element", &selectedElement, elementNames,
               IM_ARRAYSIZE(elementNames));

  ElementProperties &props = config.elements[selectedElement];

  ImGui::Separator();
  ImGui::Checkbox("Mobile", &props.mobile);
  ImGui::Checkbox("Solid", &props.solid);
  ImGui::Checkbox("Passable", &props.passable);
  ImGui::Checkbox("Rigid Body Candidate", &props.rigidBodyCandidate);
  ImGui::Checkbox("Static Terrain", &props.staticTerrain);
  ImGui::Checkbox("Body Movable", &props.bodyMovable);

  ImGui::SliderFloat("Density", &props.density, 0.0f, 5000.0f);
  ImGui::SliderFloat("Default Temp", &props.defaultTemperature, -100.0f,
                     2000.0f);
  ImGui::SliderFloat("Default Mass", &props.defaultMass, 0.0f, 10.0f);
  ImGui::SliderFloat("Default Hardness", &props.defaultHardness, 0.0f, 1000.0f);
  ImGui::SliderFloat("Default Lifetime", &props.defaultLifetime, 0.0f, 60.0f);
  ImGui::SliderFloat("Lifetime Decay", &props.lifetimeDecay, 0.0f, 1.0f);
  ImGui::SliderFloat("Default Moisture", &props.defaultMoisture, 0.0f, 10.0f);

  if (ImGui::TreeNode("Thermal Properties")) {
    ImGui::SliderFloat("Heat Capacity", &props.thermal.heatCapacity, 0.01f,
                       10.0f);
    ImGui::SliderFloat("Conductivity", &props.thermal.conductivity, 0.0f, 1.0f);
    ImGui::SliderFloat("Cooling Rate", &props.thermal.coolingRate, 0.0f, 1.0f);
    ImGui::TreePop();
  }
}

Vector2 UI::GetMouseCell() const {
  Vector2 m = View::MouseCells();
  return {std::floor(m.x), std::floor(m.y)};
}

void UI::DrawSpellBeam(const SpellStats &stats, Vector2 originCells,
                       Vector2 castDir, Color color, float worldGravity) const {
  if (stats.kind == SpellKind::Compound) {
    // Each part turns the aim its own way; undo the outer ring's turn first
    float c = std::cos(-stats.offsetRad), s = std::sin(-stats.offsetRad);
    Vector2 aim{castDir.x * c - castDir.y * s, castDir.x * s + castDir.y * c};
    for (const SpellStats &part : stats.parts)
      DrawSpellBeam(part, originCells, SpellSystem::ResolveDirection(part, aim),
                    color, worldGravity);
    return;
  }
  Vector2 o{originCells.x * CELL_SIZE, originCells.y * CELL_SIZE};
  Vector2 d = castDir;
  Vector2 n{-d.y, d.x};
  Color faint{color.r, color.g, color.b, (unsigned char)(color.a * 0.45f)};

  auto at = [&](float along, float side) {
    return Vector2{o.x + d.x * along + n.x * side,
                   o.y + d.y * along + n.y * side};
  };

  // Dotted ballistic arc from p0 with velocity v (px/s) under gravity g (px/s^2)
  auto drawArc = [&](Vector2 p0, Vector2 v, float g, float seconds,
                     Color c) {
    constexpr int kSegments = 16;
    Vector2 prev = p0;
    for (int i = 1; i <= kSegments; ++i) {
      float t = seconds * i / kSegments;
      Vector2 cur{p0.x + v.x * t, p0.y + v.y * t + 0.5f * g * t * t};
      unsigned char alpha =
          (unsigned char)(c.a * (1.0f - 0.7f * (float)i / kSegments));
      if (i % 2 == 1)
        DrawLineEx(prev, cur, 1.5f, Color{c.r, c.g, c.b, alpha});
      prev = cur;
    }
  };

  if (stats.kind == SpellKind::Flight) {
    // Where the caster gets thrown (walking and drag ignored)
    Vector2 v{d.x * stats.launchSpeed * CELL_SIZE,
              d.y * stats.launchSpeed * CELL_SIZE};
    drawArc(o, v, Character::GRAVITY * worldGravity * CELL_SIZE, 0.9f, color);
    DrawCircleLines((int)o.x, (int)o.y, 8.0f, color);
    return;
  }

  float length = stats.range * CELL_SIZE;
  float halfWidth = std::max(2.0f, stats.diameter * 0.5f * CELL_SIZE);

  if (stats.kind == SpellKind::Field) {
    // A field: dashed walls with chevrons pointing the way it moves things
    // (back toward the caster when pulling)
    DrawDashedLine(at(0, halfWidth), at(length, halfWidth), 6.0f, 1.5f, color);
    DrawDashedLine(at(0, -halfWidth), at(length, -halfWidth), 6.0f, 1.5f,
                   color);
    float spacing = std::max(14.0f, length / 8.0f);
    float chevron = std::min(halfWidth * 0.8f, 10.0f);
    float back = stats.pull > 0.0f ? -chevron : chevron;
    for (float t = spacing; t < length; t += spacing) {
      DrawLineEx(at(t - back, chevron), at(t, 0), 1.5f, faint);
      DrawLineEx(at(t - back, -chevron), at(t, 0), 1.5f, faint);
    }
    return;
  }

  const ShapeDef &shape = SpellShapes::Get(stats.shape);
  const ShapePart &lead = shape.parts.front();
  bool ball = lead.kind == ShapePart::Kind::Burst && lead.disk;
  bool figure = lead.kind == ShapePart::Kind::Burst && !lead.disk;
  if (ball) {
    // One ball flying the whole way
    float r = std::max(3.0f, std::sqrt(stats.particleCount / PI) * CELL_SIZE);
    DrawDashedLine(o, at(length, 0), 5.0f, 1.0f, faint);
    DrawCircleLinesV(at(r, 0), r, faint);
    DrawCircleLinesV(at(length, 0), r, color);
  } else if (figure || shape.weaveAmplitude > 0.0f) {
    // A leading figure (drawn as a wedge as long as its pattern) and the
    // body behind it, weaving like the emitter does
    float head = figure ? SpellShapes::ScaledRows(
                              lead, SpellShapes::PartScale(lead, stats.diameter)) *
                              lead.rowSpacing * CELL_SIZE
                        : 0.0f;
    constexpr int kSegments = 32;
    float amp = shape.weaveAmplitude * CELL_SIZE;
    Vector2 prev = at(0, 0);
    for (int i = 1; i <= kSegments; ++i) {
      float t = (length - head) * i / kSegments;
      float ticks = t / (stats.speed * CELL_SIZE) * 60.0f;
      Vector2 cur =
          at(t, amp * std::sin(ticks * 2.0f * PI / shape.weaveWavelength));
      DrawLineEx(prev, cur, 2.0f, color);
      prev = cur;
    }
    if (figure) {
      float w = 3.0f * CELL_SIZE;
      DrawTriangleLines(at(length, 0), at(length - head, w),
                        at(length - head, -w), color);
    }
  } else {
    // Element stream: beam edges and a dashed spine
    DrawLineEx(at(0, halfWidth), at(length, halfWidth), 1.5f, color);
    DrawLineEx(at(0, -halfWidth), at(length, -halfWidth), 1.5f, color);
    DrawDashedLine(o, at(length, 0), 5.0f, 1.0f, faint);
  }

  // Chevron at the end of the straight flight
  float head = std::max(8.0f, halfWidth * 1.6f);
  Vector2 tip = at(length + head * 0.6f, 0);
  DrawLineEx(at(length - head * 0.4f, halfWidth + head * 0.5f), tip, 2.0f,
             color);
  DrawLineEx(at(length - head * 0.4f, -halfWidth - head * 0.5f), tip, 2.0f,
             color);

  // Past its range the element keeps its momentum and falls
  Vector2 vel{d.x * stats.speed * CELL_SIZE, d.y * stats.speed * CELL_SIZE};
  drawArc(at(length, 0), vel, kParticleGravity * worldGravity * CELL_SIZE,
          0.5f, faint);
}

void UI::DrawActiveFields(const Simulation &sim) const {
  float time = (float)GetTime();
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Field)
      continue;
    const SpellStats &s = effect.stats;
    Vector2 d = effect.direction;
    // Streaks drift the way the field moves things
    Vector2 push = SpellSystem::FieldPush(effect);
    Vector2 n{-d.y, d.x};
    float fade = std::clamp(effect.timeRemaining / std::max(0.01f, s.duration),
                            0.0f, 1.0f);
    Color c{225, 240, 255, (unsigned char)(170 * fade)};

    // Streaks drifting along the field, staggered across its width
    int lanes = std::max(2, (int)std::ceil(s.diameter / 2.0f));
    float streak = 6.0f;
    for (int lane = 0; lane < lanes; ++lane) {
      float side = ((lane + 0.5f) / lanes - 0.5f) * s.diameter;
      float drift = s.pull > 0.0f ? -1.0f : 1.0f;
      float phase = std::fmod(time * s.speed * 0.8f + lane * 7.3f, 18.0f);
      if (drift < 0.0f)
        phase = 18.0f - phase;
      for (float along = phase; along < s.range; along += 18.0f) {
        Vector2 a{(effect.origin.x + d.x * along + n.x * side) * CELL_SIZE,
                  (effect.origin.y + d.y * along + n.y * side) * CELL_SIZE};
        Vector2 b{a.x - push.x * streak * CELL_SIZE,
                  a.y - push.y * streak * CELL_SIZE};
        DrawLineEx(b, a, 1.5f, c);
      }
    }
  }
}

void UI::DrawActiveColumns(const Simulation &sim) const {
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    const SpellStats &s = effect.stats;
    bool rising = effect.holdPhase == SpellEffect::HoldRising;
    if (s.kind != SpellKind::Element || s.holdTime <= 0.0f ||
        (!rising && effect.holdPhase != SpellEffect::HoldHolding))
      continue;
    // A drill is red, a building column gold
    Color ink = s.crush != 0.0f ? Color{196, 52, 36, 255} : Color{226, 176, 70, 255};
    // A rising column is done once built: just where it will reach, faint,
    // and what it's risen to, firm
    if (rising) {
      Vector2 d = effect.direction;
      Vector2 n{-d.y, d.x};
      float w = s.holdWidth;
      float side0 = -std::floor(w / 2.0f), side1 = side0 + w;
      auto at = [&](float ahead, float side) {
        return Vector2{(effect.origin.x + d.x * ahead + n.x * side) * CELL_SIZE,
                       (effect.origin.y + d.y * ahead + n.y * side) * CELL_SIZE};
      };
      float a0 = effect.holdGap, a1 = a0 + s.holdLength;
      float risen = a0 + effect.holdRisen;
      auto outline = [&](float from, float to, Color c) {
        Vector2 p[] = {at(from, side0), at(to, side0), at(to, side1),
                       at(from, side1)};
        for (int i = 0; i < 4; ++i)
          DrawLineEx(p[i], p[(i + 1) % 4], 1.5f, c);
      };
      outline(a0, a1, Fade(ink, 0.3f));
      outline(a0, risen, Fade(ink, 0.85f));
      continue;
    }
    // A launched block: time left to hold, as a draining ring
    if (effect.holdCells.empty())
      continue;
    Vector2 sum{0.0f, 0.0f};
    for (int32_t i : effect.holdCells) {
      sum.x += i % GRID_W + 0.5f;
      sum.y += i / GRID_W + 0.5f;
    }
    float k = CELL_SIZE / static_cast<float>(effect.holdCells.size());
    Vector2 timer{sum.x * k, sum.y * k};
    float left =
        static_cast<float>(effect.holdTicks) / std::max(1, effect.holdTotal);
    float r = 2.2f * CELL_SIZE;
    DrawRing(timer, r - 2.0f, r + 1.0f, 0.0f, 360.0f, 24, Color{38, 30, 24, 150});
    DrawRing(timer, r - 1.5f, r + 0.5f, -90.0f, -90.0f + 360.0f * left, 24, ink);
  }
}

void UI::DrawAimIndicator(const Spell &spell, Vector2 originCells,
                          Vector2 aimDir, float worldGravity) const {
  SpellStats stats = SpellSystem::Evaluate(spell);
  Vector2 castDir = SpellSystem::ResolveDirection(stats, aimDir);

  DrawSpellBeam(stats, originCells, castDir, GetSpellColor(spell),
                worldGravity);

  // World pixels: this is drawn inside the view's camera
  Vector2 screenOrigin{originCells.x * CELL_SIZE, originCells.y * CELL_SIZE};
  Vector2 mousePos = View::MouseWorld();
  float mouseDist =
      std::hypot(mousePos.x - screenOrigin.x, mousePos.y - screenOrigin.y);
  float arrowLen = std::max(40.0f, mouseDist);

  // Raw aim (faint) vs where the circle actually sends the spell (solid)
  DrawDashedLine(screenOrigin, mousePos, 6.0f, 1.5f, Theme::Rl(Tone::Verdigris, 0.55f));
  DrawCircleLines((int)screenOrigin.x, (int)screenOrigin.y, 10, BLUE);
  DrawCircleV(mousePos, 3.0f, Theme::Rl(Tone::Verdigris, 0.8f));

  Color balance = SpellEditor::BalanceColor(stats.imbalance);
  DrawArrow(screenOrigin, castDir, arrowLen, 2.5f, balance);

  float offsetDeg = stats.offsetRad * RAD2DEG;
  const char *label = "balanced";
  if (std::abs(offsetDeg) >= 0.5f) {
    // Arc from the aim to the cast direction
    float aimAngle = std::atan2(aimDir.y, aimDir.x);
    float arcRadius = std::min(48.0f, arrowLen * 0.6f);
    constexpr int kArcSegments = 16;
    Vector2 prev{screenOrigin.x + std::cos(aimAngle) * arcRadius,
                 screenOrigin.y + std::sin(aimAngle) * arcRadius};
    for (int i = 1; i <= kArcSegments; ++i) {
      float a = aimAngle + stats.offsetRad * i / kArcSegments;
      Vector2 cur{screenOrigin.x + std::cos(a) * arcRadius,
                  screenOrigin.y + std::sin(a) * arcRadius};
      DrawLineEx(prev, cur, 2.0f, balance);
      prev = cur;
    }
    label = TextFormat("%+.0f deg", offsetDeg);
  }

  Vector2 labelPos{screenOrigin.x + castDir.x * (arrowLen + 10.0f),
                   screenOrigin.y + castDir.y * (arrowLen + 10.0f)};
  Theme::DrawText(Theme::RlBody(), label, {labelPos.x, labelPos.y - 10}, 19, balance);
}

Color UI::GetSpellColor(const Spell &spell) const {
  std::uint32_t hash = 2166136261u;
  for (char c : spell.name) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 16777619u;
  }
  int r = static_cast<int>((hash >> 24) & 0xFF) % 156 + 100;
  int g = static_cast<int>((hash >> 16) & 0xFF) % 156 + 100;
  int b = static_cast<int>((hash >> 8) & 0xFF) % 156 + 100;
  return Color{static_cast<unsigned char>(r), static_cast<unsigned char>(g),
               static_cast<unsigned char>(b), 255};
}

bool UI::IsMouseOverPanel() const {
  return GetMouseY() >= BarY() || ImGui::GetIO().WantCaptureMouse;
}

const Deck *UI::HotbarDeck(const UIState &state) const {
  if (state.matchRound >= 0 && state.matchRound < MATCH_ROUNDS)
    if (const Deck *d = m_decks.Find(m_decks.Match().deckIds[state.matchRound]))
      return d;
  return m_decks.Find(m_decks.ActiveId());
}

const Spell *UI::SlotSpell(const UIState &state, int slot) const {
  if (m_hasMatchSpells && state.matchRound >= 0) {
    if (slot < 0 || slot >= DECK_SLOTS || !m_matchSpells[slot])
      return nullptr;
    return &*m_matchSpells[slot];
  }
  if (slot == 0 && !m_testSpellRef.empty() && state.matchRound < 0)
    if (const Spell *s = m_library.Find(m_testSpellRef))
      return s;
  const Deck *deck = HotbarDeck(state);
  if (!deck || slot < 0 || slot >= DECK_SLOTS)
    return nullptr;
  return m_library.Find(deck->slots[slot]);
}

const Spell *UI::GetSelectedSpell() const {
  static const UIState kDefault;
  return SlotSpell(m_lastState ? *m_lastState : kDefault, m_selectedSlot);
}

void UI::SetGameMode(bool enabled) {
  m_gameMode = enabled;
  // Match hotbars never carry the sandbox's test spell
  if (enabled)
    m_testSpellRef.clear();
}

bool UI::IsBlockingWorldInput() const {
  if (m_spellEditor.IsOpen())
    return true;
  return IsMouseOverPanel();
}
