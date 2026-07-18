#include "whas/ui/ui.h"
#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "whas/constants.h"
#include <algorithm>
#include <cmath>

UI::UI() {
  rlImGuiSetup(true);

  m_buttons[0] = {{8 + 0 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::WATER,
                  "WATER",
                  {64, 164, 223, 255}};
  m_buttons[1] = {{8 + 1 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::EARTH,
                  "EARTH",
                  {100, 60, 20, 255}};
  m_buttons[2] = {{8 + 2 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::FIRE,
                  "FIRE",
                  {220, 80, 0, 255}};
  m_buttons[3] = {{8 + 3 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::STEAM,
                  "STEAM",
                  {180, 180, 200, 255}};
  m_buttons[4] = {{8 + 4 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::CLOUD,
                  "CLOUD",
                  {220, 220, 255, 255}};
  m_buttons[5] = {{8 + 5 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::ICE,
                  "ICE",
                  {150, 240, 255, 255}};
  m_buttons[6] = {{8 + 6 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::SAND,
                  "SAND",
                  {220, 180, 100, 255}};
  m_buttons[7] = {{8 + 7 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::ROCK,
                  "ROCK",
                  {80, 80, 80, 255}};
  m_buttons[8] = {{8 + 8 * (BTN_W + BTN_PAD), PANEL_Y + 10, BTN_W, BTN_H},
                  Element::AIR,
                  "Eraser",
                  {60, 60, 60, 255}};

  m_spellButton = {(float)(WINDOW_WIDTH - SPELL_BTN_W - 8), 8.0f,
                   (float)SPELL_BTN_W, (float)SPELL_BTN_H};

  // Load available spells
  m_availableSpells = m_spellStore.LoadAll();
}

UI::~UI() { rlImGuiShutdown(); }

void UI::HandleInput(UIState &state, Simulation &sim) {
  if (m_spellEditor.IsOpen())
    return;

  if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard)
    return;

  // Handle escape to cancel aiming
  if (IsKeyPressed(KEY_ESCAPE)) {
    m_isAimingSpell = false;
  }

  // Spell casting input
  if (m_selectedSpellIndex >= 0 &&
      m_selectedSpellIndex < (int)m_availableSpells.size()) {
    Vector2 mousePos = GetMousePosition();
    Vector2 mouseCell = GetMouseCell();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      if (m_isAimingSpell) {
        // Fire the spell
        if (!IsMouseOverPanel()) {
          m_spellAimDir = {mousePos.x - m_spellOrigin.x * CELL_SIZE,
                           mousePos.y - m_spellOrigin.y * CELL_SIZE};
          float dirLen =
              std::sqrt(m_spellAimDir.x * m_spellAimDir.x +
                       m_spellAimDir.y * m_spellAimDir.y);
          if (dirLen > 0) {
            m_spellAimDir.x /= dirLen;
            m_spellAimDir.y /= dirLen;
          } else {
            m_spellAimDir = {1, 0};
          }
          // Cast the spell from world coordinates
          Vector2 spellOrigin = {m_spellOrigin.x * CELL_SIZE + CELL_SIZE / 2.0f,
                                 m_spellOrigin.y * CELL_SIZE + CELL_SIZE / 2.0f};
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
  int textX =
      (int)m_spellButton.x + (SPELL_BTN_W - MeasureText(label, 14)) / 2;
  int textY =
      (int)m_spellButton.y + (SPELL_BTN_H - 14) / 2;
  DrawText(label, textX, textY, 14, BLACK);
}

void UI::Draw(UIState &state, Simulation &sim) {
  if (state.debugOverlay) {
    sim.GetRigidBodySystem().DrawDebug();
  }

  DrawRectangle(0, PANEL_Y, WINDOW_WIDTH, PANEL_HEIGHT, Color{30, 30, 40, 255});
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

  DrawSpellButton();

  DrawSpellAimPreview();

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
  if (IsMouseOverPanel()) return;

  Vector2 cellPos = GetMouseCell();
  int cx = (int)cellPos.x;
  int cy = (int)cellPos.y;

  if (cx < 0 || cx >= GRID_W || cy < 0 || cy >= GRID_H) return;

  const Cell &cell = sim.GetCell(cx, cy);

  Vector2 mousePos = GetMousePosition();
  ImVec2 pivot = ImVec2(-0.1f, 1.1f);

  if (mousePos.y < 160) pivot.y = -0.1f;
  if (mousePos.x > WINDOW_WIDTH - 200) pivot.x = 1.1f;

  ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Always, pivot);
  ImGui::Begin("Inspector", nullptr, 
               ImGuiWindowFlags_NoTitleBar | 
               ImGuiWindowFlags_NoResize | 
               ImGuiWindowFlags_NoMove | 
               ImGuiWindowFlags_NoScrollbar | 
               ImGuiWindowFlags_NoSavedSettings | 
               ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoInputs |
               ImGuiWindowFlags_NoFocusOnAppearing |
               ImGuiWindowFlags_NoNav);

  ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Cell [%d, %d]", cx, cy);
  ImGui::Separator();
  ImGui::Text("Type: %s", GetElementName(cell.element));
  ImGui::Text("Temp: %.1f C", cell.temperature);
  ImGui::Text("Pressure: %.2f", cell.pressure);
  ImGui::Text("Velocity: (%.2f, %.2f)", cell.vx, cell.vy);
  ImGui::Text("Mass: %.2f g", cell.mass);
  ImGui::Text("Density: %.2f g/cm3", cell.density);
  if (cell.lifetime > 0) ImGui::Text("Lifetime: %.2f s", cell.lifetime);
  if (cell.moisture > 0) ImGui::Text("Moisture: %.2f", cell.moisture);

  ImGui::End();
}

const char* UI::GetElementName(Element element) const {
  switch (element) {
    case Element::AIR: return "AIR";
    case Element::WATER: return "WATER";
    case Element::EARTH: return "EARTH";
    case Element::FIRE: return "FIRE";
    case Element::STEAM: return "STEAM";
    case Element::CLOUD: return "CLOUD";
    case Element::ICE: return "ICE";
    case Element::SAND: return "SAND";
    case Element::ROCK: return "ROCK";
    default: return "UNKNOWN";
  }
}

void UI::DrawPropertyEditor(SimulationConfig &config) {
  ImGui::Begin("Simulation Config", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

  if (ImGui::CollapsingHeader("World")) {
    ImGui::SliderFloat("Gravity", &config.world.gravity, -1.0f, 1.0f);
    ImGui::SliderFloat("Pressure Eq", &config.world.pressureEq, 0.0f, 1.0f);
    ImGui::SliderFloat("Ambient Temp", &config.world.ambientTemp, -50.0f, 100.0f);
  }

  if (ImGui::CollapsingHeader("Fluid Physics")) {
    ImGui::SliderInt("Pressure Scan Depth", &config.fluid.pressureScanDepth, 1, 50);
    ImGui::SliderFloat("Pressure Weight", &config.fluid.pressureWeight, 0.0f, 2.0f);
    ImGui::SliderFloat("Gas Displacement Chance", &config.fluid.gasDisplacementChance, 0.0f, 1.0f);

    if (ImGui::TreeNode("Water Specific")) {
      ImGui::SliderFloat("Density", &config.fluid.water.density, 0.1f, 10.0f);
      ImGui::SliderFloat("Viscosity", &config.fluid.water.viscosity, 0.0f, 1.0f);
      ImGui::SliderFloat("Max Fall Speed", &config.fluid.water.maxFallSpeed, 0.0f, 20.0f);
      ImGui::SliderFloat("Max Horizontal Speed", &config.fluid.water.maxHorizontalSpeed, 0.0f, 20.0f);
      ImGui::SliderFloat("Spread Factor", &config.fluid.water.spreadFactor, 0.0f, 1.0f);
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
  const char *elementNames[] = {"AIR",   "WATER", "EARTH", "FIRE",
                                "STEAM", "CLOUD", "ICE", "SAND", "ROCK"};
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
  ImGui::SliderFloat("Default Temp", &props.defaultTemperature, -100.0f, 2000.0f);
  ImGui::SliderFloat("Default Mass", &props.defaultMass, 0.0f, 10.0f);
  ImGui::SliderFloat("Default Hardness", &props.defaultHardness, 0.0f, 1000.0f);
  ImGui::SliderFloat("Default Lifetime", &props.defaultLifetime, 0.0f, 60.0f);
  ImGui::SliderFloat("Lifetime Decay", &props.lifetimeDecay, 0.0f, 1.0f);
  ImGui::SliderFloat("Default Moisture", &props.defaultMoisture, 0.0f, 10.0f);

  if (ImGui::TreeNode("Thermal Properties")) {
    ImGui::SliderFloat("Heat Capacity", &props.thermal.heatCapacity, 0.01f, 10.0f);
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
      float range = SpellSystem::ComputeSpellRange(spell);
      float speed = SpellSystem::ComputeSpellSpeed(spell);
      ImGui::Text("Range: %.1f", range);
      ImGui::Text("Speed: %.1f", speed);
      ImGui::NewLine();
      if (ImGui::Button("Cast")) {
        m_isAimingSpell = true;
      }
    }
    ImGui::End();
  }
}

void UI::DrawSpellAimPreview() {
  if (!m_isAimingSpell)
    return;

  // Draw circle at origin
  Vector2 screenOrigin = {m_spellOrigin.x * CELL_SIZE,
                          m_spellOrigin.y * CELL_SIZE};
  DrawCircleLines((int)screenOrigin.x, (int)screenOrigin.y, 20, BLUE);

  // Draw arrow from origin to mouse
  Vector2 mousePos = GetMousePosition();
  Vector2 dir = {mousePos.x - screenOrigin.x, mousePos.y - screenOrigin.y};
  float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (dirLen > 0.1f) {
    dir.x /= dirLen;
    dir.y /= dirLen;
    DrawLineEx(screenOrigin, mousePos, 2.0f, GREEN);
    // Draw arrowhead
    Vector2 arrowBase = {mousePos.x - dir.x * 15,
                         mousePos.y - dir.y * 15};
    Vector2 perpDir = {-dir.y, dir.x};
    Vector2 arrowLeft = {arrowBase.x + perpDir.x * 8,
                         arrowBase.y + perpDir.y * 8};
    Vector2 arrowRight = {arrowBase.x - perpDir.x * 8,
                          arrowBase.y - perpDir.y * 8};
    DrawTriangle(mousePos, arrowLeft, arrowRight, GREEN);
  }
}

bool UI::IsMouseOverPanel() const {
  return GetMouseY() >= PANEL_Y || ImGui::GetIO().WantCaptureMouse;
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
