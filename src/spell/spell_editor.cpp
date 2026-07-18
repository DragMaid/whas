#include "whas/spell/spell_editor.h"
#include "whas/constants.h"
#include "whas/spell/spell_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

SpellEditor::SpellEditor() {
  // TODO: move this into mutual configuration instead
  m_library.LoadFromDirectories("assets/signs", "assets/sigils");
  m_store.EnsureDirectoryExists();
  RefreshSavedSpells();
}

void SpellEditor::RefreshSavedSpells() { m_savedSpells = m_store.LoadAll(); }

const SvgAsset *SpellEditor::GetAssetForGlyph(const PlacedGlyph &glyph) const {
  return m_library.FindById(glyph.assetId);
}

Vector2 SpellEditor::CanvasToSpellSpace(ImVec2 canvasOrigin,
                                        ImVec2 canvasCenter,
                                        ImVec2 screenPos) const {
  return {screenPos.x - canvasOrigin.x - canvasCenter.x,
          screenPos.y - canvasOrigin.y - canvasCenter.y};
}

ImVec2 SpellEditor::SpellToCanvasSpace(ImVec2 canvasOrigin, ImVec2 canvasCenter,
                                       Vector2 spellPos) const {
  return {canvasOrigin.x + canvasCenter.x + spellPos.x,
          canvasOrigin.y + canvasCenter.y + spellPos.y};
}

std::vector<LineSeg> SpellEditor::GetWorldSegments(const PlacedGlyph &glyph,
                                                   const SvgAsset &asset,
                                                   ImVec2 canvasOrigin,
                                                   ImVec2 canvasCenter) const {
  (void)canvasOrigin;
  (void)canvasCenter;
  return SpellGeometry::TransformSegments(asset.segments, asset.localCenter,
                                          glyph.position, glyph.scale,
                                          glyph.rotationDeg);
}

bool SpellEditor::IsPlacementValid(const PlacedGlyph &candidate,
                                   std::optional<size_t> ignoreIndex) const {
  const SvgAsset *asset = m_library.FindById(candidate.assetId);
  if (!asset)
    return false;

  Vector2 canvasCenter{0, 0};
  return SpellGeometry::IsGlyphPlacementValid(
      *asset, candidate, canvasCenter, SPELL_INNER_RADIUS, m_currentSpell,
      m_library.GetAssets(), ignoreIndex);
}

void SpellEditor::DrawClampedFloat(const char *label, float *value, float minV,
                                   float maxV, float step) {
  if (ImGui::SliderFloat(label, value, minV, maxV)) {
    if (step > 0.0f)
      *value = std::round(*value / step) * step;

    *value = std::clamp(*value, minV, maxV);
  }
}

void SpellEditor::DrawGlyphLines(ImDrawList *dl, const SvgAsset &asset,
                                 const PlacedGlyph &glyph, ImVec2 canvasOrigin,
                                 ImVec2 canvasCenter, ImU32 color,
                                 float thickness) {
  auto segments = SpellGeometry::TransformSegments(
      asset.segments, asset.localCenter, glyph.position, glyph.scale,
      glyph.rotationDeg);
  for (const auto &seg : segments) {
    ImVec2 a = SpellToCanvasSpace(canvasOrigin, canvasCenter, seg.a);
    ImVec2 b = SpellToCanvasSpace(canvasOrigin, canvasCenter, seg.b);
    dl->AddLine(a, b, color, thickness);
  }
}

// TODO: write the draw whole spell thumbnail - just a nested call of this
// function
void SpellEditor::DrawAssetThumbnail(const SvgAsset &asset, bool selected) {
  ImVec2 size(72, 72);
  ImGui::PushID(asset.id.c_str());

  bool anyInvalid = false;
  for (size_t i = 0; i < m_currentSpell.glyphs.size(); ++i) {
    if (!IsPlacementValid(m_currentSpell.glyphs[i], i)) {
      anyInvalid = true;
      break;
    }
  }

  if (anyInvalid) ImGui::BeginDisabled();
  if (ImGui::Selectable("##thumb", selected, 0, size)) {
    m_paletteAssetId = asset.id;
    m_isPlacing = true;
    m_selectedGlyphIndex = -1;
    m_ghostScale = 1.0f;
    m_ghostRotation = 0.0f;
  }
  if (anyInvalid) ImGui::EndDisabled();

  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 255));
  dl->AddRect(p0, p1, IM_COL32(80, 80, 80, 255));

  float pad = 8.0f;
  float availW = size.x - pad * 2;
  float availH = size.y - pad * 2;
  float scale = std::min(availW / asset.viewWidth, availH / asset.viewHeight);
  ImVec2 thumbCenter{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};

  for (const auto &seg : asset.segments) {
    Vector2 a = SpellGeometry::TransformPoint(seg.a, asset.localCenter, {0, 0},
                                              scale, 0.0f);
    Vector2 b = SpellGeometry::TransformPoint(seg.b, asset.localCenter, {0, 0},
                                              scale, 0.0f);
    dl->AddLine({thumbCenter.x + a.x, thumbCenter.y + a.y},
                {thumbCenter.x + b.x, thumbCenter.y + b.y},
                IM_COL32(0, 0, 0, 255), 1.5f);
  }

  ImGui::SetCursorScreenPos({p0.x, p1.y + 2});
  ImGui::TextUnformatted(asset.id.c_str());
  ImGui::PopID();
}

bool SpellEditor::TryPlaceAt(Vector2 spellPos) {
  if (!m_isPlacing || m_paletteAssetId.empty())
    return false;

  const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
  if (!asset)
    return false;

  PlacedGlyph glyph;
  glyph.assetId = m_paletteAssetId;
  glyph.kind = asset->kind;
  glyph.position = spellPos;
  glyph.scale = m_ghostScale;
  glyph.rotationDeg = m_ghostRotation;

  if (!IsPlacementValid(glyph, std::nullopt)) {
    return false;
  }

  m_currentSpell.glyphs.push_back(glyph);
  m_isPlacing = false;
  m_paletteAssetId.clear();
  return true;
}

bool SpellEditor::TrySelectAt(Vector2 spellPos) {
  for (int i = static_cast<int>(m_currentSpell.glyphs.size()) - 1; i >= 0;
       --i) {
    const PlacedGlyph &g = m_currentSpell.glyphs[i];
    const SvgAsset *asset = GetAssetForGlyph(g);
    if (!asset)
      continue;
    if (SpellGeometry::HitTestGlyph(g, *asset, spellPos, 12.0f)) {
      m_selectedGlyphIndex = i;
      m_isPlacing = false;
      m_paletteAssetId.clear();
      return true;
    }
  }
  return false;
}

void SpellEditor::DrawCanvas(ImVec2 canvasOrigin, ImVec2 canvasSize) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 canvasEnd = {canvasOrigin.x + canvasSize.x,
                      canvasOrigin.y + canvasSize.y};
  ImVec2 canvasCenter = {canvasSize.x * 0.5f, canvasSize.y * 0.5f};
  ImVec2 circleCenter = {canvasOrigin.x + canvasCenter.x,
                         canvasOrigin.y + canvasCenter.y};

  dl->AddRectFilled(canvasOrigin, canvasEnd, IM_COL32(40, 40, 50, 255));

  dl->AddCircleFilled(circleCenter, SPELL_OUTER_RADIUS,
                      IM_COL32(255, 255, 255, 255));
  dl->AddCircle(circleCenter, SPELL_INNER_RADIUS, IM_COL32(0, 0, 0, 255), 64,
                2.0f);

  for (size_t i = 0; i < m_currentSpell.glyphs.size(); ++i) {
    const PlacedGlyph &glyph = m_currentSpell.glyphs[i];
    const SvgAsset *asset = GetAssetForGlyph(glyph);
    if (!asset)
      continue;

    // Check validity for coloring (red if invalid)
    bool valid = IsPlacementValid(glyph, i);
    ImU32 color;
    if (static_cast<int>(i) == m_selectedGlyphIndex) {
      color = valid ? IM_COL32(80, 200, 80, 255) : IM_COL32(220, 40, 40, 255);
    } else {
      color = valid ? IM_COL32(0, 0, 0, 255) : IM_COL32(220, 40, 40, 255);
    }
    DrawGlyphLines(dl, *asset, glyph, canvasOrigin, canvasCenter, color, 2.0f);
  }

  ImGui::SetCursorScreenPos(canvasOrigin);
  ImGui::InvisibleButton("##spell_canvas", canvasSize);
  bool canvasHovered = ImGui::IsItemHovered();

  Vector2 spellMouse =
      CanvasToSpellSpace(canvasOrigin, canvasCenter, ImGui::GetIO().MousePos);

  if (m_isPlacing && canvasHovered) {
    const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
    if (asset) {
      PlacedGlyph ghost;
      ghost.assetId = m_paletteAssetId;
      ghost.kind = asset->kind;
      ghost.position = spellMouse;
      ghost.scale = m_ghostScale;
      ghost.rotationDeg = m_ghostRotation;

      if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        m_ghostRotation += 45.0f;
        if (m_ghostRotation > GLYPH_ROTATION_MAX) m_ghostRotation -= 360.0f;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        m_ghostScale += 0.1f;
        m_ghostScale = std::min(m_ghostScale, GLYPH_SCALE_MAX);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        m_ghostScale = std::max(GLYPH_SCALE_MIN, m_ghostScale - 0.1f);
      }
      
      ghost.scale = m_ghostScale;
      ghost.rotationDeg = m_ghostRotation;

      bool valid = IsPlacementValid(ghost, std::nullopt);
      ImU32 ghostColor =
          valid ? IM_COL32(0, 0, 0, 180) : IM_COL32(220, 40, 40, 200);
      DrawGlyphLines(dl, *asset, ghost, canvasOrigin, canvasCenter, ghostColor,
                     2.0f);
    }
  }

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    if (m_isPlacing) {
      TryPlaceAt(spellMouse);
    } else if (!TrySelectAt(spellMouse)) {
      m_selectedGlyphIndex = -1;
    }
  }

  if (!m_isPlacing && m_selectedGlyphIndex >= 0) {
    PlacedGlyph &selectedGlyph = m_currentSpell.glyphs[m_selectedGlyphIndex];
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
      selectedGlyph.rotationDeg += 45.0f;
      if (selectedGlyph.rotationDeg > GLYPH_ROTATION_MAX) selectedGlyph.rotationDeg -= 360.0f;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
      selectedGlyph.scale += 0.1f;
      selectedGlyph.scale = std::min(selectedGlyph.scale, GLYPH_SCALE_MAX);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
      selectedGlyph.scale = std::max(GLYPH_SCALE_MIN, selectedGlyph.scale - 0.1f);
    }

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      selectedGlyph.position = spellMouse;
      // Allow moving even if placement becomes invalid; visual feedback will show red.
      // Validation still updates for feedback elsewhere.
    }
  }
}

void SpellEditor::DrawPalette() {
  auto drawSection = [&](const char *title, GlyphKind kind) {
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
      return;

    auto assets = m_library.GetByKind(kind);
    if (assets.empty()) {
      ImGui::TextDisabled("None available");
      return;
    }

    ImGui::PushID(title);
    ImGui::BeginChild(title, ImVec2(0, 160), true);
    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(panelWidth / 84.0f));

    ImGui::Columns(columns, nullptr, false);
    for (const SvgAsset *asset : assets) {
      bool selected = m_isPlacing && m_paletteAssetId == asset->id;
      DrawAssetThumbnail(*asset, selected);
      ImGui::NextColumn();
    }
    ImGui::Columns(1);
    ImGui::EndChild();
    ImGui::PopID();
  };

  drawSection("Signs", GlyphKind::Sign);
  drawSection("Sigils", GlyphKind::Sigil);
}

void SpellEditor::DrawEditPanel() {
  if (m_selectedGlyphIndex < 0 ||
      m_selectedGlyphIndex >= static_cast<int>(m_currentSpell.glyphs.size()))
    return;

  PlacedGlyph &glyph = m_currentSpell.glyphs[m_selectedGlyphIndex];
  const SvgAsset *asset = GetAssetForGlyph(glyph);
  if (!asset)
    return;

  ImGui::Separator();
  ImGui::Text("Editing: %s", asset->id.c_str());

  // Ensure rotation is in 45-degree steps
  float rotateStep = 45.0f;
  float scale = glyph.scale;
  float rotation = glyph.rotationDeg;
  // Round to nearest 45 degrees
  rotation = std::round(rotation / rotateStep) * 45.0f;
  // Clamp rotation within allowed range (0-360)
  if (rotation < GLYPH_ROTATION_MIN)
    rotation = GLYPH_ROTATION_MIN;
  if (rotation > GLYPH_ROTATION_MAX)
    rotation = GLYPH_ROTATION_MAX;

  DrawClampedFloat("Scale", &scale, GLYPH_SCALE_MIN, GLYPH_SCALE_MAX, 0.1f);
  DrawClampedFloat("Rotation", &rotation, GLYPH_ROTATION_MIN,
                   GLYPH_ROTATION_MAX, rotateStep);

  glyph.scale = scale;
  glyph.rotationDeg = rotation;

  if (ImGui::Button("Remove")) {
    m_currentSpell.glyphs.erase(m_currentSpell.glyphs.begin() +
                                m_selectedGlyphIndex);
    m_selectedGlyphIndex = -1;
  }
}

// TODO: do this to show thumbnail
void SpellEditor::DrawSavedSpells() {
  if (!ImGui::CollapsingHeader("Saved Spells", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  ImGui::BeginChild("SavedSpellsList", ImVec2(0, 120), true);
  if (m_savedSpells.empty()) {
    ImGui::TextDisabled("No saved spells");
  } else {
    for (const Spell &s : m_savedSpells) {
      if (ImGui::Selectable(s.name.c_str())) {
        m_currentSpell = s;
        std::strncpy(m_nameBuffer, s.name.c_str(), SPELL_NAME_MAX_LEN);
        m_nameBuffer[SPELL_NAME_MAX_LEN] = '\0';
        m_selectedGlyphIndex = -1;
        m_isPlacing = false;
        m_paletteAssetId.clear();
      }
    }
  }
  ImGui::EndChild();
}

void SpellEditor::DrawOverlay() {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({(float)WINDOW_WIDTH, (float)WINDOW_HEIGHT});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.92f));
  ImGui::Begin("SpellEditorOverlay", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  ImGui::Text("Spell Editor");
  float buttonsWidth =
      ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2 +
      ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Save Spell").x +
      ImGui::GetStyle().FramePadding.x * 2;

  ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonsWidth +
                  ImGui::GetCursorPosX());
  if (ImGui::Button("Close")) {
    m_open = false;
    m_isPlacing = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Save Spell")) {
    // Validate spell before saving
    int signCount = 0, sigilCount = 0;
    bool anyInvalid = false;
    for (size_t i = 0; i < m_currentSpell.glyphs.size(); ++i) {
      const PlacedGlyph &g = m_currentSpell.glyphs[i];
      const SvgAsset *a = GetAssetForGlyph(g);
      if (!a) { anyInvalid = true; break; }
      if (a->kind == GlyphKind::Sign) signCount++;
      if (a->kind == GlyphKind::Sigil) sigilCount++;
      if (!IsPlacementValid(g, i)) { anyInvalid = true; }
    }
    if (signCount == 0 || sigilCount == 0) {
      m_statusMessage = "Spell must contain at least one sign and one sigil.";
    } else if (anyInvalid) {
      m_statusMessage = "Spell contains invalid glyph placements (red).";
    } else {
      m_currentSpell.name = m_nameBuffer;
      std::string err;
      if (m_store.Save(m_currentSpell, err)) {
        m_statusMessage = "Saved.";
        RefreshSavedSpells();
      } else {
        m_statusMessage = err;
      }
    }
  }

  ImGui::SetNextItemWidth(240);
  ImGui::InputText("Name", m_nameBuffer, SPELL_NAME_MAX_LEN + 1);
  if (!m_statusMessage.empty())
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s",
                       m_statusMessage.c_str());

  ImGui::Separator();

  float rightW = 280.0f;
  float leftW = ImGui::GetContentRegionAvail().x - rightW - 8.0f;

  ImGui::BeginChild("LeftPanel", ImVec2(leftW, 0), false);
  ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.y = std::max(canvasSize.y, SPELL_OUTER_RADIUS * 2.0f + 40.0f);
  DrawCanvas(canvasOrigin, canvasSize);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("RightPanel", ImVec2(rightW, 0), true);
  DrawEditPanel();
  DrawPalette();
  DrawSavedSpells();
  ImGui::EndChild();

  ImGui::End();
  ImGui::PopStyleColor();
}

void SpellEditor::Draw() {
  if (!m_open)
    return;
  DrawOverlay();
}
