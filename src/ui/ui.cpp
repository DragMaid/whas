#include "whas/ui/ui.h"
#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "whas/constants.h"
#include "whas/game/character.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

Vector2 ComputeSpellAimDirection(const Vector2 &spellOrigin,
                                 const Vector2 &mousePos) {
  Vector2 screenOrigin = {spellOrigin.x * CELL_SIZE + CELL_SIZE / 2.0f,
                          spellOrigin.y * CELL_SIZE + CELL_SIZE / 2.0f};
  Vector2 mouseDirPixels = {mousePos.x - screenOrigin.x,
                            mousePos.y - screenOrigin.y};
  Vector2 mouseDirCells = {mouseDirPixels.x / CELL_SIZE,
                           mouseDirPixels.y / CELL_SIZE};
  float dirLen = std::sqrt(mouseDirCells.x * mouseDirCells.x +
                           mouseDirCells.y * mouseDirCells.y);
  if (dirLen > 0.0001f) {
    return {mouseDirCells.x / dirLen, mouseDirCells.y / dirLen};
  }
  return {1.0f, 0.0f};
}

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
  rlImGuiSetup(true);

  struct Material {
    Element element;
    const char *label;
    Color col;
  };
  static constexpr Material kMaterials[] = {
      {Element::WATER, "WATER", {64, 164, 223, 255}},
      {Element::EARTH, "EARTH", {100, 60, 20, 255}},
      {Element::FIRE, "FIRE", {220, 80, 0, 255}},
      {Element::STEAM, "STEAM", {180, 180, 200, 255}},
      {Element::CLOUD, "CLOUD", {220, 220, 255, 255}},
      {Element::ICE, "ICE", {150, 240, 255, 255}},
      {Element::SAND, "SAND", {220, 180, 100, 255}},
      {Element::ROCK, "ROCK", {80, 80, 80, 255}},
      {Element::WOOD, "WOOD", {120, 78, 40, 255}},
      {Element::GRASS, "GRASS", {70, 150, 55, 255}},
      {Element::AIR, "Eraser", {60, 60, 60, 255}},
  };
  static_assert(std::size(kMaterials) == std::size(decltype(m_buttons){}));
  for (size_t i = 0; i < std::size(kMaterials); ++i) {
    const Material &m = kMaterials[i];
    m_buttons[i] = {{(float)(8 + i * (BTN_W + BTN_PAD)), PANEL_Y + 10,
                     (float)BTN_W, (float)BTN_H},
                    m.element,
                    m.label,
                    m.col};
  }

  m_spellButton = {(float)(WINDOW_WIDTH - SPELL_BTN_W - 8), 8.0f,
                   (float)SPELL_BTN_W, (float)SPELL_BTN_H};

  // Load available spells
  m_availableSpells = m_spellStore.LoadAll();
}

UI::~UI() { rlImGuiShutdown(); }

void UI::HandleInput(UIState &state, Simulation &sim) {
  if (m_spellEditor.IsOpen()) {
    m_editorWasOpen = true;
    return;
  }
  if (m_editorWasOpen) {
    m_editorWasOpen = false;
    m_availableSpells = m_spellStore.LoadAll();
    m_selectedSpellIndex = -1;
    m_isAimingSpell = false;
  }

  if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard)
    return;

  // Handle escape to cancel aiming
  if (IsKeyPressed(KEY_ESCAPE)) {
    m_isAimingSpell = false;
  }

  if (m_gameMode) {
    // The character owns the mouse; only keep the debug toggles and editor
    if (IsKeyPressed(KEY_F3))
      state.debugOverlay = !state.debugOverlay;
    if (IsKeyPressed(KEY_F4))
      state.showConfigEditor = !state.showConfigEditor;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), m_spellButton))
      m_spellEditor.Open();
    return;
  }

  // Spell casting input
  if (m_selectedSpellIndex >= 0 &&
      m_selectedSpellIndex < (int)m_availableSpells.size()) {
    Vector2 mousePos = GetMousePosition();
    Vector2 mouseCell = GetMouseCell();

    if (m_isAimingSpell) {
      m_spellAimDir = ComputeSpellAimDirection(m_spellOrigin, mousePos);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      if (m_isAimingSpell) {
        // Fire the spell
        if (!IsMouseOverPanel()) {
          Vector2 spellOrigin = {m_spellOrigin.x + 0.5f,
                                 m_spellOrigin.y + 0.5f};
          sim.CastSpell(m_availableSpells[m_selectedSpellIndex], spellOrigin,
                        m_spellAimDir);
          m_isAimingSpell = false;
        }
      } else if (!IsMouseOverPanel()) {
        // Start aiming
        if (CheckCollisionPointRec(mousePos, m_spellButton)) {
          m_spellEditor.Open();
          return;
        }
        m_spellOrigin = mouseCell;
        m_isAimingSpell = true;
      }
    }
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();
    if (CheckCollisionPointRec(m, m_spellButton)) {
      m_spellEditor.Open();
      return;
    }
  }

  if (IsKeyPressed(KEY_ONE))
    state.selectedMaterial = Element::WATER;
  if (IsKeyPressed(KEY_TWO))
    state.selectedMaterial = Element::EARTH;
  if (IsKeyPressed(KEY_THREE))
    state.selectedMaterial = Element::FIRE;
  if (IsKeyPressed(KEY_FOUR))
    state.selectedMaterial = Element::STEAM;
  if (IsKeyPressed(KEY_FIVE))
    state.selectedMaterial = Element::CLOUD;
  if (IsKeyPressed(KEY_SIX))
    state.selectedMaterial = Element::ICE;
  if (IsKeyPressed(KEY_SEVEN))
    state.selectedMaterial = Element::SAND;
  if (IsKeyPressed(KEY_EIGHT))
    state.selectedMaterial = Element::ROCK;
  if (IsKeyPressed(KEY_NINE))
    state.selectedMaterial = Element::AIR;

  if (IsKeyPressed(KEY_F3))
    state.debugOverlay = !state.debugOverlay;
  if (IsKeyPressed(KEY_F4))
    state.showConfigEditor = !state.showConfigEditor;

  float wheel = GetMouseWheelMove();
  if (wheel != 0) {
    state.brushRadius = std::clamp(state.brushRadius + (int)wheel, 1, 20);
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    Vector2 m = GetMousePosition();
    for (const auto &btn : m_buttons) {
      if (CheckCollisionPointRec(m, btn.rect)) {
        state.selectedMaterial = btn.element;
      }
    }
  }
}

void UI::DrawSpellButton() {
  bool hovered = CheckCollisionPointRec(GetMousePosition(), m_spellButton);
  DrawRectangleRec(m_spellButton, hovered ? Color{200, 200, 220, 255} : WHITE);
  DrawRectangleLinesEx(m_spellButton, 2, BLACK);
  const char *label = "Spells";
  int textX = (int)m_spellButton.x + (SPELL_BTN_W - MeasureText(label, 14)) / 2;
  int textY = (int)m_spellButton.y + (SPELL_BTN_H - 14) / 2;
  DrawText(label, textX, textY, 14, BLACK);
}

void UI::Draw(UIState &state, Simulation &sim) {
  if (state.debugOverlay) {
    sim.GetRigidBodySystem().DrawDebug();
  }

  if (!m_gameMode) {
    DrawRectangle(0, PANEL_Y, WINDOW_WIDTH, PANEL_HEIGHT,
                  Color{30, 30, 40, 255});
    DrawLine(0, PANEL_Y, WINDOW_WIDTH, PANEL_Y, DARKGRAY);

    for (const auto &btn : m_buttons) {
      bool selected = (state.selectedMaterial == btn.element);
      DrawRectangleRec(btn.rect, selected ? WHITE : btn.col);
      DrawRectangleLinesEx(btn.rect, 2, selected ? BLACK : DARKGRAY);

      int textX = (int)btn.rect.x + (BTN_W - MeasureText(btn.label, 14)) / 2;
      int textY = (int)btn.rect.y + (BTN_H - 14) / 2;
      DrawText(btn.label, textX, textY, 14, selected ? BLACK : WHITE);
    }

    DrawText(TextFormat("Brush: %d", state.brushRadius), WINDOW_WIDTH - 120,
             PANEL_Y + 20, 16, RAYWHITE);
  }

  DrawSpellButton();

  DrawActiveGusts(sim);
  DrawSpellAimPreview(sim);

  rlImGuiBegin();

  DrawSpellSelectionPanel();

  if (state.showConfigEditor && !m_spellEditor.IsOpen()) {
    DrawPropertyEditor(sim.GetConfig());
  }

  if (!m_spellEditor.IsOpen())
    DrawInspector(sim);

  m_spellEditor.Draw();

  rlImGuiEnd();
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
  if (mousePos.x > WINDOW_WIDTH - 200)
    pivot.x = 1.1f;

  ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always, pivot);
  ImGui::Begin(
      "Inspector", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNav);

  ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Cell [%d, %d]", cx, cy);
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
  Vector2 m = GetMousePosition();
  return {std::floor(m.x / CELL_SIZE), std::floor(m.y / CELL_SIZE)};
}

void UI::DrawSpellSelectionPanel() {
  if (ImGui::Begin("Spells", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Available Spells:");
    for (int i = 0; i < (int)m_availableSpells.size(); ++i) {
      bool selected = (m_selectedSpellIndex == i);
      if (ImGui::Selectable(m_availableSpells[i].name.c_str(), selected)) {
        m_selectedSpellIndex = i;
        m_isAimingSpell = false;
      }
    }
    ImGui::Separator();
    if (m_selectedSpellIndex >= 0 &&
        m_selectedSpellIndex < (int)m_availableSpells.size()) {
      const Spell &spell = m_availableSpells[m_selectedSpellIndex];
      SpellStats stats = SpellSystem::Evaluate(spell);
      SpellEditor::DrawStats(stats);
      ImGui::NewLine();
      if (m_gameMode) {
        ImGui::TextDisabled("Click in the world to queue this spell");
      } else {
        ImGui::BeginDisabled(!stats.valid);
        if (ImGui::Button("Cast")) {
          m_isAimingSpell = true;
        }
        ImGui::EndDisabled();
      }
    }
    ImGui::End();
  }
}

void UI::DrawSpellAimPreview(const Simulation &sim) {
  if (!m_isAimingSpell)
    return;
  const Spell *spell = GetSelectedSpell();
  if (!spell)
    return;

  DrawAimIndicator(*spell, {m_spellOrigin.x + 0.5f, m_spellOrigin.y + 0.5f},
                   m_spellAimDir, sim.GetConfig().world.gravity);
}

void UI::DrawSpellBeam(const SpellStats &stats, Vector2 originCells,
                       Vector2 castDir, Color color, float worldGravity) const {
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

  if (stats.kind == SpellKind::Gust) {
    // A wind field: dashed walls with chevrons blowing along it
    DrawDashedLine(at(0, halfWidth), at(length, halfWidth), 6.0f, 1.5f, color);
    DrawDashedLine(at(0, -halfWidth), at(length, -halfWidth), 6.0f, 1.5f,
                   color);
    float spacing = std::max(14.0f, length / 8.0f);
    float chevron = std::min(halfWidth * 0.8f, 10.0f);
    for (float t = spacing; t < length; t += spacing) {
      DrawLineEx(at(t - chevron, chevron), at(t, 0), 1.5f, faint);
      DrawLineEx(at(t - chevron, -chevron), at(t, 0), 1.5f, faint);
    }
    return;
  }

  // Element stream: beam edges and a dashed spine
  DrawLineEx(at(0, halfWidth), at(length, halfWidth), 1.5f, color);
  DrawLineEx(at(0, -halfWidth), at(length, -halfWidth), 1.5f, color);
  DrawDashedLine(o, at(length, 0), 5.0f, 1.0f, faint);

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

void UI::DrawActiveGusts(const Simulation &sim) const {
  float time = (float)GetTime();
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Gust)
      continue;
    const SpellStats &s = effect.stats;
    Vector2 d = effect.direction;
    Vector2 n{-d.y, d.x};
    float fade = std::clamp(effect.timeRemaining / std::max(0.01f, s.duration),
                            0.0f, 1.0f);
    Color c{225, 240, 255, (unsigned char)(170 * fade)};

    // Streaks drifting along the field, staggered across its width
    int lanes = std::max(2, (int)std::ceil(s.diameter / 2.0f));
    float streak = 6.0f;
    for (int lane = 0; lane < lanes; ++lane) {
      float side = ((lane + 0.5f) / lanes - 0.5f) * s.diameter;
      float phase = std::fmod(time * s.speed * 0.8f + lane * 7.3f, s.range);
      for (float along = phase; along < s.range; along += 18.0f) {
        Vector2 a{(effect.origin.x + d.x * along + n.x * side) * CELL_SIZE,
                  (effect.origin.y + d.y * along + n.y * side) * CELL_SIZE};
        Vector2 b{a.x - d.x * streak * CELL_SIZE, a.y - d.y * streak * CELL_SIZE};
        DrawLineEx(b, a, 1.5f, c);
      }
    }
  }
}

void UI::DrawAimIndicator(const Spell &spell, Vector2 originCells,
                          Vector2 aimDir, float worldGravity) const {
  SpellStats stats = SpellSystem::Evaluate(spell);
  Vector2 castDir = SpellSystem::ResolveDirection(stats, aimDir);

  DrawSpellBeam(stats, originCells, castDir, GetSpellColor(spell),
                worldGravity);

  Vector2 screenOrigin{originCells.x * CELL_SIZE, originCells.y * CELL_SIZE};
  Vector2 mousePos = GetMousePosition();
  float mouseDist =
      std::hypot(mousePos.x - screenOrigin.x, mousePos.y - screenOrigin.y);
  float arrowLen = std::max(40.0f, mouseDist);

  // Raw aim (faint) vs where the circle actually sends the spell (solid)
  DrawDashedLine(screenOrigin, mousePos, 6.0f, 1.5f, Color{120, 160, 255, 140});
  DrawCircleLines((int)screenOrigin.x, (int)screenOrigin.y, 10, BLUE);
  DrawCircleV(mousePos, 3.0f, Color{120, 160, 255, 200});

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
  DrawText(label, (int)labelPos.x, (int)labelPos.y - 8, 16, balance);
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
  return GetMouseY() >= PANEL_Y || ImGui::GetIO().WantCaptureMouse;
}

const Spell *UI::GetSelectedSpell() const {
  if (m_selectedSpellIndex < 0 ||
      m_selectedSpellIndex >= (int)m_availableSpells.size())
    return nullptr;
  return &m_availableSpells[m_selectedSpellIndex];
}

void UI::SetGameMode(bool enabled) {
  m_gameMode = enabled;
  m_isAimingSpell = false;
}

bool UI::IsBlockingWorldInput() const {
  if (m_spellEditor.IsOpen())
    return true;
  if (m_isAimingSpell)
    return true;
  if (CheckCollisionPointRec(GetMousePosition(), m_spellButton))
    return true;
  return IsMouseOverPanel();
}
