#include "whas/spell/spell_editor.h"
#include "whas/constants.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/spell_geometry.h"
#include "whas/ui/spell_thumbnails.h"
#include <algorithm>
#include <cmath>
#include <cstring>

SpellEditor::SpellEditor() {
  // TODO: move this into mutual configuration instead
  m_library.LoadFromDirectories("assets/signs", "assets/sigils");
}

void SpellEditor::Bind(SpellLibrary *spells, DeckBook *decks) {
  m_spells = spells;
  m_decks = decks;
}

bool SpellEditor::TakeTestRequest(std::string &ref) {
  if (m_testRef.empty())
    return false;
  ref = std::move(m_testRef);
  m_testRef.clear();
  return true;
}

namespace {

void AddArrow(ImDrawList *dl, ImVec2 from, ImVec2 dir, float length,
              ImU32 color, float thickness) {
  ImVec2 tip{from.x + dir.x * length, from.y + dir.y * length};
  dl->AddLine(from, tip, color, thickness);
  float head = std::min(12.0f, length * 0.4f);
  ImVec2 back{tip.x - dir.x * head, tip.y - dir.y * head};
  ImVec2 side{-dir.y * head * 0.5f, dir.x * head * 0.5f};
  dl->AddTriangleFilled(tip, {back.x + side.x, back.y + side.y},
                        {back.x - side.x, back.y - side.y}, color);
}

ImU32 ToImU32(Color c) { return IM_COL32(c.r, c.g, c.b, c.a); }

} // namespace

Color SpellEditor::BalanceColor(float imbalance) {
  if (imbalance < SpellSystem::BALANCED_THRESHOLD)
    return {60, 200, 90, 255};
  float t = std::clamp(imbalance, 0.0f, 1.0f);
  return {255, static_cast<unsigned char>(170 - 140 * t), 30, 255};
}

namespace {

const char *KindLabel(const SpellStats &stats) {
  switch (stats.kind) {
  case SpellKind::Flight:
    return "Wind Underfoot";
  case SpellKind::Gust:
    return "Wind";
  case SpellKind::Element:
    return ElementName(stats.element);
  default:
    return "?";
  }
}

void DrawLayeredStats(const SpellStats &stats) {
  int parts = static_cast<int>(stats.parts.size());
  if (!stats.valid)
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "1-%d parts, each with one sigil;\nthe ring holds "
                       "signs only",
                       LAYER_MAX_COMPONENTS);
  ImGui::Text("Layered: %d part%s, cast together", parts,
              parts == 1 ? "" : "s");
  Color c = SpellEditor::BalanceColor(stats.imbalance);
  ImGui::TextColored(ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f),
                     "Ring balance: %.0f%%  Offset: %+.0f deg",
                     (1.0f - stats.imbalance) * 100.0f,
                     stats.offsetRad * RAD2DEG);
  int ticks = TurnController::CastTicks(stats);
  if (ticks > TurnController::TURN_TICKS)
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "Cast: %.2fs, longer than a turn",
                       ticks * TurnController::TICK_DT);
  else
    ImGui::Text("Cast: %.2fs", ticks * TurnController::TICK_DT);
  for (int i = 0; i < parts; ++i) {
    ImGui::PushID(i);
    if (ImGui::TreeNode("part", "Part %d: %s", i + 1,
                        KindLabel(stats.parts[i]))) {
      SpellEditor::DrawStats(stats.parts[i]);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

} // namespace

void SpellEditor::DrawStats(const SpellStats &stats) {
  if (stats.kind == SpellKind::Compound) {
    DrawLayeredStats(stats);
    return;
  }
  if (!stats.valid) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "Needs exactly one known sigil");
    return;
  }
  Color c = BalanceColor(stats.imbalance);
  switch (stats.kind) {
  case SpellKind::Flight:
    ImGui::Text("Wind Underfoot: carries the caster");
    break;
  case SpellKind::Gust:
    ImGui::Text("Wind: pushes everything in its path");
    break;
  default:
    ImGui::Text("Element: %s", ElementName(stats.element));
    break;
  }
  ImGui::TextColored(ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f),
                     "Balance: %.0f%%  Offset: %+.0f deg",
                     (1.0f - stats.imbalance) * 100.0f,
                     stats.offsetRad * RAD2DEG);
  if (stats.kind == SpellKind::Flight) {
    ImGui::Text("Launch: %.0f cells/s", stats.launchSpeed);
    return;
  }
  ImGui::Text("Speed: %.0f cells/s", stats.speed);
  ImGui::Text("Range: %.0f cells", stats.range);
  if (stats.kind == SpellKind::Gust) {
    ImGui::Text("Force: %.0f  Duration: %.2fs", stats.force, stats.duration);
    ImGui::Text("Width: %.1f cells", stats.diameter);
    return;
  }
  ImGui::Text("Density: %.2f  Power: %.0f", stats.density, stats.power);
  ImGui::Text("Diameter: %.1f  Particles: %d", stats.diameter,
              stats.particleCount);
  if (stats.temperature > 0.0f)
    ImGui::Text("Heat: %.0f C", stats.temperature);
  if (stats.shape != SpellShape::Stream) {
    const ShapeDef &shape = SpellShapes::Get(stats.shape);
    ImGui::Text("Shape: %s", shape.name);
    // A figure takes a set amount of material: the sigil gives some,
    // collection draws in more (when there's that much nearby)
    int need = SpellShapes::MaterialNeeded(shape, stats.diameter);
    if (need > stats.particleCount + stats.collectMax)
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                         "Needs %d %s, has %d%s: it'll be cut short.\n"
                         "Add collection signs (or a bigger sigil).",
                         need, ElementName(stats.element), stats.particleCount,
                         stats.collectMax > 0
                             ? TextFormat(" + up to %d collected",
                                          stats.collectMax)
                             : "");
    else if (need > stats.particleCount)
      ImGui::Text("Needs %d %s: %d + collection, if there's enough nearby",
                  need, ElementName(stats.element), stats.particleCount);
    else if (need >= 0)
      ImGui::Text("Needs %d %s, has %d", need, ElementName(stats.element),
                  stats.particleCount);
  }
  if (stats.temperatureDelta < 0.0f)
    ImGui::Text("Cooled: %.0f C", stats.temperatureDelta);
  if (stats.hardnessScale != 1.0f)
    ImGui::Text("Lands x%.2f as hard", stats.hardnessScale);
  if (stats.crush > 0.0f)
    ImGui::Text("Crushes what it hits to sand (%.1f)", stats.crush);
  else if (stats.crush < 0.0f)
    ImGui::Text("Reforms sand into earth (%.1f)", -stats.crush);
  if (stats.restore > 0.0f)
    ImGui::Text("Restores what it hits (%.1f)", stats.restore);
  if (stats.collectMax > 0)
    ImGui::Text("Collects up to %d cells within %.0f", stats.collectMax,
                stats.collectRadius);
}


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
  return SpellGeometry::GlyphSegments(asset, glyph);
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
  for (const auto &seg : SpellGeometry::GlyphSegments(asset, glyph)) {
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
    ClearSelection();
    m_paletteAssetId = asset.id;
    m_isPlacing = true;
    m_ghostScale = 1.0f;
    m_ghostRotation = 0.0f;
    m_ghostInverted = false;
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

void SpellEditor::ClearSelection() {
  m_isPlacing = false;
  m_paletteAssetId.clear();
  m_paletteSpellRef.clear();
  m_selectedGlyphIndex = -1;
  m_selectedComponent = -1;
}

bool SpellEditor::TryPlaceAt(Vector2 spellPos) {
  if (!m_isPlacing)
    return false;
  if (!m_paletteSpellRef.empty())
    return TryPlaceComponentAt(spellPos);
  if (m_paletteAssetId.empty())
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
  glyph.inverted =
      m_ghostInverted && SpellSystem::SignInvertible(m_paletteAssetId);

  if (!IsPlacementValid(glyph, std::nullopt)) {
    return false;
  }

  if (glyph.kind == GlyphKind::Sigil) {
    if (m_currentSpell.Layered()) {
      m_statusMessage = "The outer ring holds signs only.";
      return false;
    }
    // One sigil for what the spell is, and at most one dragon for its shape
    bool shape = SpellSystem::IsShapeSigil(glyph.assetId);
    if (std::any_of(m_currentSpell.glyphs.begin(), m_currentSpell.glyphs.end(),
                    [shape](const PlacedGlyph &g) {
                      return g.kind == GlyphKind::Sigil &&
                             SpellSystem::IsShapeSigil(g.assetId) == shape;
                    })) {
      m_statusMessage = shape ? "A spell can only hold one dragon sigil."
                              : "A spell can only hold one element sigil "
                                "(plus a dragon).";
      return false;
    }
  }

  m_currentSpell.glyphs.push_back(glyph);
  m_isPlacing = false;
  m_paletteAssetId.clear();
  return true;
}

bool SpellEditor::CanAddComponent(std::string *why) const {
  auto fail = [why](const char *message) {
    if (why)
      *why = message;
    return false;
  };
  if (!m_currentSpell.Layered() && !m_currentSpell.glyphs.empty())
    return fail("Layered spells start from an empty circle: press New, "
                "place spells, then ring signs.");
  if ((int)m_currentSpell.components.size() >= LAYER_MAX_COMPONENTS)
    return fail("A layered spell holds at most 5 spells.");
  return true;
}

std::optional<SpellComponent>
SpellEditor::GhostComponent(Vector2 spellPos) const {
  const Spell *source = m_spells ? m_spells->Find(m_paletteSpellRef) : nullptr;
  if (!source || source->Layered())
    return std::nullopt;
  return SpellComponent{source->name, source->glyphs, spellPos,
                        m_ghostComponentScale, m_ghostRotation};
}

bool SpellEditor::TryPlaceComponentAt(Vector2 spellPos) {
  auto component = GhostComponent(spellPos);
  if (!component || !SpellGeometry::IsComponentPlacementValid(
                        *component, m_currentSpell))
    return false;
  if (!CanAddComponent(&m_statusMessage))
    return false;
  m_currentSpell.components.push_back(std::move(*component));
  m_isPlacing = false;
  m_paletteSpellRef.clear();
  m_selectedComponent = (int)m_currentSpell.components.size() - 1;
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
      ClearSelection();
      m_selectedGlyphIndex = i;
      return true;
    }
  }
  for (int i = static_cast<int>(m_currentSpell.components.size()) - 1; i >= 0;
       --i) {
    const SpellComponent &c = m_currentSpell.components[i];
    if (std::hypot(spellPos.x - c.position.x, spellPos.y - c.position.y) <=
        SpellGeometry::ComponentRadius(c.scale)) {
      ClearSelection();
      m_selectedComponent = i;
      return true;
    }
  }
  return false;
}

void SpellEditor::DrawComponent(ImDrawList *dl,
                                const SpellComponent &component,
                                ImVec2 canvasOrigin, ImVec2 canvasCenter,
                                ImU32 color) {
  ImVec2 c = SpellToCanvasSpace(canvasOrigin, canvasCenter, component.position);
  dl->AddCircle(c, SpellGeometry::ComponentRadius(component.scale), color, 48,
                2.0f);
  dl->AddCircle(c, SPELL_INNER_RADIUS * component.scale, color, 48, 1.0f);
  for (const PlacedGlyph &glyph : component.glyphs) {
    const SvgAsset *asset = GetAssetForGlyph(glyph);
    if (!asset)
      continue;
    DrawGlyphLines(dl, *asset, SpellGeometry::ComponentGlyph(component, glyph),
                   canvasOrigin, canvasCenter, color, 1.5f);
  }
  // Which way it fires, relative to the circle's aim
  float rad = component.rotationDeg * DEG2RAD;
  ImVec2 dir{std::sin(rad), -std::cos(rad)};
  float r = SpellGeometry::ComponentRadius(component.scale);
  AddArrow(dl, {c.x + dir.x * r, c.y + dir.y * r}, dir, 14.0f,
           IM_COL32(90, 90, 200, 200), 2.0f);
}

namespace {

// R turns by 45 degrees, wrapping into -180..180
void Turn(float &rotationDeg) {
  rotationDeg += 45.0f;
  if (rotationDeg > GLYPH_ROTATION_MAX)
    rotationDeg -= 360.0f;
}

// W / E grow and shrink by `step` within [minV, maxV]
void Resize(float &scale, float step, float minV, float maxV) {
  if (ImGui::IsKeyPressed(ImGuiKey_W))
    scale = std::min(scale + step, maxV);
  if (ImGui::IsKeyPressed(ImGuiKey_E))
    scale = std::max(minV, scale - step);
}

} // namespace

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
  bool layered = m_currentSpell.Layered();
  if (layered) {
    // The core holds the embedded spells, the band around it the ring signs
    dl->AddCircle(circleCenter, LAYER_CORE_RADIUS, IM_COL32(0, 0, 0, 255), 64,
                  2.0f);
    dl->AddCircle(circleCenter, LAYER_RING_INNER, IM_COL32(0, 0, 0, 255), 64,
                  1.0f);
  } else {
    dl->AddCircle(circleCenter, SPELL_INNER_RADIUS, IM_COL32(0, 0, 0, 255), 64,
                  2.0f);
  }

  for (size_t i = 0; i < m_currentSpell.components.size(); ++i) {
    const SpellComponent &component = m_currentSpell.components[i];
    bool valid = SpellGeometry::IsComponentPlacementValid(
        component, m_currentSpell, i);
    bool selected = static_cast<int>(i) == m_selectedComponent;
    ImU32 color = !valid    ? IM_COL32(220, 40, 40, 255)
                  : selected ? IM_COL32(80, 200, 80, 255)
                             : IM_COL32(0, 0, 0, 255);
    DrawComponent(dl, component, canvasOrigin, canvasCenter, color);
  }

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

  DrawVectorOverlay(dl, canvasOrigin, canvasCenter);

  ImGui::SetCursorScreenPos(canvasOrigin);
  ImGui::InvisibleButton("##spell_canvas", canvasSize);
  bool canvasHovered = ImGui::IsItemHovered();

  Vector2 spellMouse =
      CanvasToSpellSpace(canvasOrigin, canvasCenter, ImGui::GetIO().MousePos);

  if (m_isPlacing && canvasHovered && !m_paletteSpellRef.empty()) {
    if (ImGui::IsKeyPressed(ImGuiKey_R))
      Turn(m_ghostRotation);
    Resize(m_ghostComponentScale, 0.05f, COMPONENT_SCALE_MIN,
           COMPONENT_SCALE_MAX);
    if (auto ghost = GhostComponent(spellMouse)) {
      bool valid = CanAddComponent() && SpellGeometry::IsComponentPlacementValid(
                                            *ghost, m_currentSpell);
      DrawComponent(dl, *ghost, canvasOrigin, canvasCenter,
                    valid ? IM_COL32(0, 0, 0, 150) : IM_COL32(220, 40, 40, 180));
    }
  } else if (m_isPlacing && canvasHovered) {
    const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
    if (asset) {
      if (ImGui::IsKeyPressed(ImGuiKey_R))
        Turn(m_ghostRotation);
      Resize(m_ghostScale, 0.1f, GLYPH_SCALE_MIN, GLYPH_SCALE_MAX);
      if (ImGui::IsKeyPressed(ImGuiKey_F) &&
          SpellSystem::SignInvertible(m_paletteAssetId))
        m_ghostInverted = !m_ghostInverted;

      PlacedGlyph ghost;
      ghost.assetId = m_paletteAssetId;
      ghost.kind = asset->kind;
      ghost.position = spellMouse;
      ghost.scale = m_ghostScale;
      ghost.rotationDeg = m_ghostRotation;
      ghost.inverted = m_ghostInverted;

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
      ClearSelection();
    }
  }

  if (!m_isPlacing && m_selectedComponent >= 0 &&
      m_selectedComponent < (int)m_currentSpell.components.size()) {
    SpellComponent &component = m_currentSpell.components[m_selectedComponent];
    if (ImGui::IsKeyPressed(ImGuiKey_R))
      Turn(component.rotationDeg);
    Resize(component.scale, 0.05f, COMPONENT_SCALE_MIN, COMPONENT_SCALE_MAX);
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      component.position = spellMouse; // turns red while out of place
  }

  if (!m_isPlacing && m_selectedGlyphIndex >= 0) {
    PlacedGlyph &selectedGlyph = m_currentSpell.glyphs[m_selectedGlyphIndex];
    if (ImGui::IsKeyPressed(ImGuiKey_R))
      Turn(selectedGlyph.rotationDeg);
    Resize(selectedGlyph.scale, 0.1f, GLYPH_SCALE_MIN, GLYPH_SCALE_MAX);
    if (ImGui::IsKeyPressed(ImGuiKey_F) &&
        SpellSystem::SignInvertible(selectedGlyph.assetId))
      selectedGlyph.inverted = !selectedGlyph.inverted;

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      selectedGlyph.position = spellMouse;
      // Allow moving even if placement becomes invalid; visual feedback will show red.
      // Validation still updates for feedback elsewhere.
    }
  }
}

void SpellEditor::DrawVectorOverlay(ImDrawList *dl, ImVec2 canvasOrigin,
                                    ImVec2 canvasCenter) {
  ImVec2 center = SpellToCanvasSpace(canvasOrigin, canvasCenter, {0, 0});

  // "Up" on the circle is where the caster aims
  ImVec2 aimMark{center.x, center.y - SPELL_OUTER_RADIUS + 6.0f};
  dl->AddTriangleFilled({aimMark.x, aimMark.y - 8.0f},
                        {aimMark.x - 6.0f, aimMark.y + 4.0f},
                        {aimMark.x + 6.0f, aimMark.y + 4.0f},
                        IM_COL32(90, 90, 200, 200));

  for (const auto &glyph : m_currentSpell.glyphs) {
    if (glyph.kind != GlyphKind::Sign)
      continue;
    float rad = glyph.rotationDeg * DEG2RAD;
    ImVec2 dir{std::sin(rad), -std::cos(rad)};
    ImVec2 from = SpellToCanvasSpace(canvasOrigin, canvasCenter, glyph.position);
    AddArrow(dl, from, dir, 30.0f * glyph.scale, IM_COL32(70, 110, 220, 200),
             2.0f);
  }

  SpellStats stats = SpellSystem::Evaluate(m_currentSpell);
  if (stats.totalMagnitude <= 0.0f)
    return;

  ImU32 color = ToImU32(BalanceColor(stats.imbalance));
  float netLen = std::hypot(stats.netLocal.x, stats.netLocal.y);
  if (stats.imbalance < SpellSystem::BALANCED_THRESHOLD || netLen < 0.001f) {
    dl->AddCircle(center, 10.0f, color, 24, 3.0f);
    return;
  }
  ImVec2 dir{stats.netLocal.x / netLen, stats.netLocal.y / netLen};
  AddArrow(dl, center, dir, std::min(SPELL_INNER_RADIUS * 0.8f, netLen * 60.0f),
           color, 4.0f);
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
  DrawSpellPalette();
}

// Saved single-layer spells, to embed in a layered one
void SpellEditor::DrawSpellPalette() {
  if (!m_spells ||
      !ImGui::CollapsingHeader("Spells (layer)", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  std::string why;
  if (!CanAddComponent(&why)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", why.c_str());
    ImGui::PopTextWrapPos();
    return;
  }

  ImGui::BeginChild("SpellPalette", ImVec2(0, 160), true);
  float panelWidth = ImGui::GetContentRegionAvail().x;
  int columns = std::max(1, static_cast<int>(panelWidth / 84.0f));
  int shown = 0;
  for (const Spell &spell : m_spells->All()) {
    if (spell.Layered() || !SpellSystem::Evaluate(spell).valid)
      continue;
    std::string ref = m_spells->RefOf(spell);
    if (shown++ % columns != 0)
      ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushID(ref.c_str());
    bool selected = m_isPlacing && m_paletteSpellRef == ref;
    ImVec2 size(72, 72);
    if (ImGui::Selectable("##spell", selected, 0, size)) {
      ClearSelection();
      m_paletteSpellRef = ref;
      m_isPlacing = true;
      m_ghostRotation = 0.0f;
    }
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(26, 26, 36, 255));
    if (m_thumbnails)
      m_thumbnails->Draw(dl, spell, {p0.x + 4, p0.y + 4},
                         {p1.x - 4, p1.y - 4});
    ImGui::PushClipRect({p0.x, p1.y}, {p1.x, p1.y + 20}, true);
    ImGui::TextUnformatted(spell.name.c_str());
    ImGui::PopClipRect();
    ImGui::PopID();
    ImGui::EndGroup();
  }
  if (shown == 0)
    ImGui::TextDisabled("Save a single-layer spell first.");
  ImGui::EndChild();
}

void SpellEditor::DrawComponentPanel() {
  SpellComponent &component = m_currentSpell.components[m_selectedComponent];
  ImGui::Separator();
  ImGui::Text("Layer: %s", component.source.empty() ? "(spell)"
                                                    : component.source.c_str());
  float rotation = component.rotationDeg;
  DrawClampedFloat("Scale", &component.scale, COMPONENT_SCALE_MIN,
                   COMPONENT_SCALE_MAX, 0.05f);
  DrawClampedFloat("Rotation", &rotation, GLYPH_ROTATION_MIN,
                   GLYPH_ROTATION_MAX, 45.0f);
  component.rotationDeg = rotation;
  ImGui::TextDisabled("Strength: %.0f%%",
                      SpellSystem::ComponentEffectiveness(component.scale) *
                          100.0f);
  if (ImGui::Button("Remove")) {
    m_currentSpell.components.erase(m_currentSpell.components.begin() +
                                    m_selectedComponent);
    m_selectedComponent = -1;
  }
}

void SpellEditor::DrawEditPanel() {
  if (m_isPlacing && !m_paletteSpellRef.empty()) {
    ImGui::Separator();
    ImGui::Text("Placing spell: %s", m_paletteSpellRef.c_str());
    DrawClampedFloat("Scale", &m_ghostComponentScale, COMPONENT_SCALE_MIN,
                     COMPONENT_SCALE_MAX, 0.05f);
    DrawClampedFloat("Rotation", &m_ghostRotation, GLYPH_ROTATION_MIN,
                     GLYPH_ROTATION_MAX, 45.0f);
    ImGui::TextDisabled("Strength: %.0f%%",
                        SpellSystem::ComponentEffectiveness(
                            m_ghostComponentScale) *
                            100.0f);
    if (ImGui::Button("Cancel"))
      ClearSelection();
    return;
  }

  if (m_selectedComponent >= 0 &&
      m_selectedComponent < (int)m_currentSpell.components.size()) {
    DrawComponentPanel();
    return;
  }

  if (m_isPlacing && !m_paletteAssetId.empty()) {
    const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
    if (!asset) return;

    ImGui::Separator();
    ImGui::Text("Placing: %s", asset->id.c_str());

    float rotateStep = 45.0f;
    float scale = m_ghostScale;
    float rotation = m_ghostRotation;
    
    rotation = std::round(rotation / rotateStep) * 45.0f;
    if (rotation < GLYPH_ROTATION_MIN) rotation = GLYPH_ROTATION_MIN;
    if (rotation > GLYPH_ROTATION_MAX) rotation = GLYPH_ROTATION_MAX;

    DrawClampedFloat("Scale", &scale, GLYPH_SCALE_MIN, GLYPH_SCALE_MAX, 0.1f);
    DrawClampedFloat("Rotation", &rotation, GLYPH_ROTATION_MIN, GLYPH_ROTATION_MAX, rotateStep);

    m_ghostScale = scale;
    m_ghostRotation = rotation;
    if (SpellSystem::SignInvertible(asset->id))
      ImGui::Checkbox("Inverted (F)", &m_ghostInverted);

    if (ImGui::Button("Cancel")) {
      m_isPlacing = false;
      m_paletteAssetId.clear();
    }
    return;
  }

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
  if (SpellSystem::SignInvertible(asset->id))
    ImGui::Checkbox("Inverted (F)", &glyph.inverted);

  if (ImGui::Button("Remove")) {
    m_currentSpell.glyphs.erase(m_currentSpell.glyphs.begin() +
                                m_selectedGlyphIndex);
    m_selectedGlyphIndex = -1;
  }
}

void SpellEditor::OpenSpell(const Spell &spell) {
  m_currentSpell = spell;
  std::strncpy(m_nameBuffer, spell.name.c_str(), SPELL_NAME_MAX_LEN);
  m_nameBuffer[SPELL_NAME_MAX_LEN] = '\0';
  ClearSelection();
  m_statusMessage.clear();
  m_tab = Tab::Edit;
  m_switchTab = true;
}

void SpellEditor::SaveCurrent() {
  int signCount = 0, sigilCount = 0, shapeCount = 0;
  bool anyInvalid = false;
  for (size_t i = 0; i < m_currentSpell.glyphs.size(); ++i) {
    const PlacedGlyph &g = m_currentSpell.glyphs[i];
    const SvgAsset *a = GetAssetForGlyph(g);
    if (!a) {
      anyInvalid = true;
      break;
    }
    if (a->kind == GlyphKind::Sign)
      signCount++;
    else if (SpellSystem::IsShapeSigil(a->id))
      shapeCount++;
    else
      sigilCount++;
    if (!IsPlacementValid(g, i))
      anyInvalid = true;
  }
  for (size_t i = 0; i < m_currentSpell.components.size(); ++i)
    if (!SpellGeometry::IsComponentPlacementValid(m_currentSpell.components[i],
                                                  m_currentSpell, i))
      anyInvalid = true;

  if (m_currentSpell.Layered()) {
    if ((int)m_currentSpell.components.size() > LAYER_MAX_COMPONENTS) {
      m_statusMessage = "A layered spell holds at most 5 spells.";
      return;
    }
    if (sigilCount + shapeCount > 0) {
      m_statusMessage = "The outer ring holds signs only.";
      return;
    }
    if (!SpellSystem::Evaluate(m_currentSpell).valid) {
      m_statusMessage = "Every embedded spell needs exactly one sigil.";
      return;
    }
  } else if (signCount == 0 || sigilCount != 1 || shapeCount > 1) {
    m_statusMessage =
        "Spell must contain at least one sign and exactly one sigil.";
    return;
  }
  if (anyInvalid) {
    m_statusMessage = "Spell contains invalid placements (red).";
    return;
  }
  m_currentSpell.name = m_nameBuffer;
  std::string err;
  m_statusMessage = m_spells->Save(m_currentSpell, err) ? "Saved." : err;
}

void SpellEditor::DrawOverlay() {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({(float)WINDOW_WIDTH, (float)WINDOW_HEIGHT});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.96f));
  ImGui::Begin("SpellEditorOverlay", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  // Close sits at the right of the tab row
  float closeW = ImGui::CalcTextSize("Close (Esc)").x +
                 ImGui::GetStyle().FramePadding.x * 2;
  ImVec2 row = ImGui::GetCursorPos();
  ImGui::SetCursorPos({ImGui::GetWindowWidth() - closeW - 10, row.y});
  if (ImGui::Button("Close (Esc)") ||
      (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive() &&
       !m_openPopup)) {
    m_open = false;
    m_isPlacing = false;
  }
  ImGui::SetCursorPos(row);

  if (ImGui::BeginTabBar("EditorTabs")) {
    ImGuiTabItemFlags editFlags =
        m_switchTab && m_tab == Tab::Edit ? ImGuiTabItemFlags_SetSelected : 0;
    ImGuiTabItemFlags libFlags = m_switchTab && m_tab == Tab::Library
                                     ? ImGuiTabItemFlags_SetSelected
                                     : 0;
    m_switchTab = false;
    if (ImGui::BeginTabItem("Edit spell", nullptr, editFlags)) {
      m_tab = Tab::Edit;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Library & decks", nullptr, libFlags)) {
      m_tab = Tab::Library;
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  if (m_tab == Tab::Library) {
    DrawLibraryTab();
    ImGui::End();
    ImGui::PopStyleColor();
    return;
  }

  ImGui::SetNextItemWidth(240);
  ImGui::InputText("Name", m_nameBuffer, SPELL_NAME_MAX_LEN + 1);
  ImGui::SameLine();
  if (ImGui::Button("Save spell"))
    SaveCurrent();
  ImGui::SameLine();
  if (ImGui::Button("New")) {
    m_currentSpell = {};
    m_nameBuffer[0] = '\0';
    ClearSelection();
    m_statusMessage.clear();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_spells || !m_spells->Find(m_spells->RefOf(m_nameBuffer)));
  if (ImGui::Button("Test in sandbox"))
    m_testRef = m_spells->RefOf(m_nameBuffer);
  ImGui::EndDisabled();
  if (!m_statusMessage.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s",
                       m_statusMessage.c_str());
  }

  ImGui::Separator();

  float rightW = 280.0f;
  float leftW = ImGui::GetContentRegionAvail().x - rightW - 8.0f;
  constexpr float stripH = 64.0f;

  ImGui::BeginChild("LeftPanel", ImVec2(leftW, 0), false);
  ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.y -= stripH + 6.0f;
  DrawCanvas(canvasOrigin, canvasSize);
  ImGui::SetCursorScreenPos({canvasOrigin.x, canvasOrigin.y + canvasSize.y + 6});
  DrawPreviewStrip({leftW, stripH});
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("RightPanel", ImVec2(rightW, 0), true);
  if (ImGui::CollapsingHeader("Spell Stats", ImGuiTreeNodeFlags_DefaultOpen))
    DrawStats(SpellSystem::Evaluate(m_currentSpell));
  DrawEditPanel();
  DrawPalette();
  ImGui::EndChild();

  ImGui::End();
  ImGui::PopStyleColor();
}

void SpellEditor::Draw() {
  if (!m_open)
    return;
  DrawOverlay();
}
