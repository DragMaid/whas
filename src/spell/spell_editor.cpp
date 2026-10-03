#include "whas/spell/spell_editor.h"
#include "whas/audio/audio_manager.h"
#include "whas/engine/view.h"
#include "whas/game/turn_controller.h"
#include "whas/spell/glyph_docs.h"
#include "whas/spell/spell_geometry.h"
#include "whas/ui/spell_thumbnails.h"
#include "whas/ui/widgets.h"
#include "whas/spell/spell_rules.h"
#include "whas/ui/theme.h"
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

using Theme::Tone;

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

// Shortcuts don't fire while typing a name
bool Typing() { return ImGui::GetIO().WantTextInput; }

// An open ring around `center` (canvas position of the spell's centre),
// spell units drawn `zoom` pixels each
void AddRing(ImDrawList *dl, const SpellGeometry::Ring &ring, ImVec2 center,
             float zoom, ImU32 color, float thickness) {
  auto points = SpellGeometry::RingPoints(ring);
  std::vector<ImVec2> screen;
  screen.reserve(points.size());
  for (Vector2 p : points)
    screen.push_back({center.x + p.x * zoom, center.y + p.y * zoom});
  dl->AddPolyline(screen.data(), (int)screen.size(), color, ImDrawFlags_None,
                  thickness);
}

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
  case SpellKind::Field:
    return stats.element == Element::AIR
               ? "Wind field"
               : TextFormat("%s field", ElementName(stats.element));
  case SpellKind::Element:
    return ElementName(stats.element);
  default:
    return "?";
  }
}

void DrawProblem(const char *problem) {
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::Vec(Tone::Oxblood), "%s", problem);
  ImGui::PopTextWrapPos();
}

void DrawLayeredStats(const SpellStats &stats, const char *problem) {
  int parts = static_cast<int>(stats.parts.size());
  if (!stats.valid)
    DrawProblem(problem && *problem
                    ? problem
                    : "Each part needs one sigil; the ring holds signs only");
  ImGui::Text("Layered: %d part%s, cast together", parts,
              parts == 1 ? "" : "s");
  Color c = SpellEditor::BalanceColor(stats.imbalance);
  ImGui::TextColored(ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f),
                     "Ring balance: %.0f%%  Offset: %+.0f deg",
                     (1.0f - stats.imbalance) * 100.0f,
                     stats.offsetRad * RAD2DEG);
  int ticks = TurnController::CastTicks(stats);
  if (ticks > TurnController::TURN_TICKS)
    ImGui::TextColored(Theme::Vec(Tone::Oxblood),
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

void SpellEditor::DrawStats(const SpellStats &stats, const char *problem) {
  if (stats.kind == SpellKind::Compound) {
    DrawLayeredStats(stats, problem);
    return;
  }
  if (!stats.valid) {
    DrawProblem(problem && *problem ? problem
                                    : "Needs exactly one known sigil");
    return;
  }
  Color c = BalanceColor(stats.imbalance);
  switch (stats.kind) {
  case SpellKind::Flight:
    ImGui::Text("Wind Underfoot: carries the caster");
    break;
  case SpellKind::Field:
    if (stats.element == Element::AIR)
      ImGui::Text("Wind field: %s everything loose",
                  stats.pull > 0.0f ? "pulls in" : "pushes away");
    else
      ImGui::Text("%s field: %s existing %s only",
                  stats.pull > 0.0f ? "Pull" : "Push",
                  stats.pull > 0.0f ? "pulls in" : "pushes away",
                  ElementName(stats.element));
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
  if (stats.kind == SpellKind::Field) {
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
      ImGui::TextColored(Theme::Vec(Tone::Brass),
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
  if (stats.flashRadius > 0.0f)
    ImGui::Text("Flash: blinds within %.0f cells for %.1fs", stats.flashRadius,
                stats.flashTime);
  if (stats.steerTime > 0.0f)
    ImGui::Text("Sights set: follows your cursor for %.1fs (%.0f deg/s)",
                stats.steerTime, stats.steerRate * RAD2DEG);
  if (stats.homeTarget == HomeTarget::Human)
    ImGui::Text("Guided: chases the nearest enemy (%.0f deg/s)",
                stats.homeTurnRate * RAD2DEG);
  else if (stats.homeTarget == HomeTarget::Element)
    ImGui::Text("Guided: chases the nearest %s (%.0f deg/s)",
                ElementName(stats.homeElement), stats.homeTurnRate * RAD2DEG);
  if (stats.temperatureDelta < 0.0f)
    ImGui::Text("Cooled: %.0f C", stats.temperatureDelta);
  if (stats.hardnessScale != 1.0f)
    ImGui::Text("Lands x%.2f as hard", stats.hardnessScale);
  if (stats.crush > 0.0f)
    ImGui::Text("Crushes (%.1f): rock and earth burst out as sand, %d "
                "cells around the hit",
                stats.crush, std::min(4, static_cast<int>(stats.crush)));
  else if (stats.crush < 0.0f)
    ImGui::Text("Reforms sand into earth (%.1f)", -stats.crush);
  if (stats.restore > 0.0f)
    ImGui::Text("Restores what it hits (%.1f)", stats.restore);
  if (stats.collectMax > 0)
    ImGui::Text("Collects up to %d cells within %.0f", stats.collectMax,
                stats.collectRadius);
  if (stats.holdTime > 0.0f)
    ImGui::Text("Column: a %.0f x %.0f block held %.1fs, %s",
                stats.holdLength, stats.holdWidth, stats.holdTime,
                stats.speed > 0.0f ? "launched by levitation"
                                   : "formed in front of you");
}


const SvgAsset *SpellEditor::GetAssetForGlyph(const PlacedGlyph &glyph) const {
  return m_library.FindById(glyph.assetId);
}

Vector2 SpellEditor::CanvasToSpellSpace(ImVec2 canvasOrigin,
                                        ImVec2 canvasCenter,
                                        ImVec2 screenPos) const {
  return {(screenPos.x - canvasOrigin.x - canvasCenter.x) / m_zoom,
          (screenPos.y - canvasOrigin.y - canvasCenter.y) / m_zoom};
}

ImVec2 SpellEditor::SpellToCanvasSpace(ImVec2 canvasOrigin, ImVec2 canvasCenter,
                                       Vector2 spellPos) const {
  return {canvasOrigin.x + canvasCenter.x + spellPos.x * m_zoom,
          canvasOrigin.y + canvasCenter.y + spellPos.y * m_zoom};
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

namespace {

// Name, kind and what it does, for a palette card or a selection
void GlyphTooltip(const SvgAsset &asset) {
  GlyphDocs::Info info = GlyphDocs::Get(asset.id);
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
  ImGui::TextColored(Theme::Vec(Tone::Brass), "%s",
                     info.name ? info.name : asset.id.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled(asset.kind == GlyphKind::Sigil ? "sigil" : "sign");
  if (*info.text)
    ImGui::TextUnformatted(info.text);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

} // namespace

// A palette card: click to pick it up, click again to put it back
void SpellEditor::DrawAssetThumbnail(const SvgAsset &asset, bool selected) {
  float ui = View::UiScale();
  ImVec2 size(72 * ui, 72 * ui);
  ImGui::PushID(asset.id.c_str());

  // The card of the glyph selected on the canvas is highlighted too
  bool onCanvas =
      m_selectedGlyphIndex >= 0 &&
      m_selectedGlyphIndex < (int)m_currentSpell.glyphs.size() &&
      m_currentSpell.glyphs[m_selectedGlyphIndex].assetId == asset.id;
  if (ImGui::Selectable("##thumb", selected || onCanvas, 0, size)) {
    ClearSelection();
    if (!selected) {
      m_paletteAssetId = asset.id;
      m_isPlacing = true;
      m_ghostScale = 1.0f;
      m_ghostRotation = 0.0f;
      m_ghostInverted = false;
    }
  }
  if (ImGui::IsItemHovered())
    GlyphTooltip(asset);
  if (onCanvas && m_scrollToCard) {
    ImGui::SetScrollHereY(0.5f);
    m_scrollToCard = false;
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  dl->AddRectFilled(p0, p1, IM_COL32(236, 226, 200, 255), 4.0f);
  if (selected || onCanvas)
    dl->AddRect(p0, p1, selected ? Theme::U32(Tone::Brass)
                                 : Theme::U32(Tone::Verdigris),
                4.0f, 0, 3.0f);
  else
    dl->AddRect(p0, p1, Theme::U32(Tone::Line), 4.0f);

  float pad = 8.0f * ui;
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
                IM_COL32(34, 24, 16, 255), 1.5f * ui);
  }

  const char *name = GlyphDocs::Get(asset.id).name;
  ImGui::PushClipRect({p0.x, p1.y}, {p1.x + 8 * ui, p1.y + 40 * ui}, true);
  ImGui::SetCursorScreenPos({p0.x, p1.y + 2});
  ImGui::TextUnformatted(name ? name : asset.id.c_str());
  ImGui::PopClipRect();
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

  auto ghost = GhostGlyph(spellPos);
  if (!ghost)
    return false;
  PlacedGlyph glyph = *ghost;

  if (!IsPlacementValid(glyph, std::nullopt)) {
    return false;
  }

  if (glyph.kind == GlyphKind::Sigil) {
    if (m_currentSpell.Layered()) {
      m_statusMessage = "The outer ring holds signs only.";
      return false;
    }
    // At most one dragon, guidance and human; two element sigils only make
    // sense with guidance (the smaller is the target), so no more than two
    auto count = [this](auto pred) {
      return std::count_if(m_currentSpell.glyphs.begin(),
                           m_currentSpell.glyphs.end(),
                           [&](const PlacedGlyph &g) {
                             return g.kind == GlyphKind::Sigil && pred(g.assetId);
                           });
    };
    auto isElement = [](const std::string &id) {
      return !SpellSystem::IsShapeSigil(id) && id != "guidance" &&
             id != "human";
    };
    const std::string &id = glyph.assetId;
    if (!isElement(id) && count([&](const std::string &g) { return g == id; })) {
      m_statusMessage = "A spell holds only one " + id + " sigil.";
      return false;
    }
    if (isElement(id) && count(isElement) >= 2) {
      m_statusMessage = "At most two element sigils (with guidance, the "
                        "smaller one is its target).";
      return false;
    }
  }

  m_currentSpell.glyphs.push_back(glyph);
  m_isPlacing = false;
  m_paletteAssetId.clear();
  return true;
}

std::optional<PlacedGlyph> SpellEditor::GhostGlyph(Vector2 spellPos) const {
  const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
  if (!asset)
    return std::nullopt;
  PlacedGlyph glyph;
  glyph.assetId = m_paletteAssetId;
  glyph.kind = asset->kind;
  glyph.position = spellPos;
  glyph.scale = m_ghostScale;
  glyph.rotationDeg = m_ghostRotation;
  glyph.inverted =
      m_ghostInverted && SpellSystem::SignInvertible(m_paletteAssetId);
  return glyph;
}

void SpellEditor::DrawHints(ImDrawList *dl, ImVec2 canvasOrigin,
                            ImVec2 canvasSize) {
  const char *hint;
  bool invertible = false;
  if (m_isPlacing && !m_paletteSpellRef.empty()) {
    hint = "Click: place   R / Shift+wheel: turn   W E / wheel: size   "
           "Right-click / Esc: put back";
  } else if (m_isPlacing) {
    invertible = SpellSystem::SignInvertible(m_paletteAssetId);
    hint = "Click: place   R / Shift+wheel: turn   W E / wheel: size   "
           "Right-click / Esc: put back";
  } else if (m_selectedGlyphIndex >= 0 || m_selectedComponent >= 0) {
    if (m_selectedGlyphIndex >= 0 &&
        m_selectedGlyphIndex < (int)m_currentSpell.glyphs.size())
      invertible = SpellSystem::SignInvertible(
          m_currentSpell.glyphs[m_selectedGlyphIndex].assetId);
    hint = "Drag: move   R / Shift+wheel: turn   W E / wheel: size   "
           "Del: remove   Right-click / Esc: deselect";
  } else {
    hint = "Pick a sigil, sign or spell on the right   Click a glyph to "
           "select it   Ctrl+Z / Ctrl+Y: undo / redo";
  }
  std::string text = hint;
  if (invertible)
    text += "   F: invert";
  ImVec2 size = ImGui::CalcTextSize(text.c_str());
  ImVec2 p{canvasOrigin.x + 8, canvasOrigin.y + canvasSize.y - size.y - 8};
  dl->AddRectFilled({p.x - 4, p.y - 2}, {p.x + size.x + 4, p.y + size.y + 2},
                    Theme::U32(Tone::Ink, 0.8f), 4.0f);
  dl->AddText(p, Theme::U32(Tone::Parchment), text.c_str());
}

void SpellEditor::RemoveSelected() {
  if (m_selectedGlyphIndex >= 0 &&
      m_selectedGlyphIndex < (int)m_currentSpell.glyphs.size())
    m_currentSpell.glyphs.erase(m_currentSpell.glyphs.begin() +
                                m_selectedGlyphIndex);
  else if (m_selectedComponent >= 0 &&
           m_selectedComponent < (int)m_currentSpell.components.size())
    m_currentSpell.components.erase(m_currentSpell.components.begin() +
                                    m_selectedComponent);
  else
    return;
  ClearSelection();
}

void SpellEditor::HandleShortcuts() {
  if (Typing() || m_openPopup)
    return;
  const ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    io.KeyShift ? Redo() : Undo();
  else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    Redo();
  if (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
      ImGui::IsKeyPressed(ImGuiKey_Backspace))
    RemoveSelected();
}

namespace {

bool SameGlyphs(const std::vector<PlacedGlyph> &a,
                const std::vector<PlacedGlyph> &b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](const PlacedGlyph &x, const PlacedGlyph &y) {
                      return x.assetId == y.assetId && x.kind == y.kind &&
                             x.position.x == y.position.x &&
                             x.position.y == y.position.y &&
                             x.scale == y.scale &&
                             x.rotationDeg == y.rotationDeg &&
                             x.inverted == y.inverted;
                    });
}

bool SameSpell(const Spell &a, const Spell &b) {
  return SameGlyphs(a.glyphs, b.glyphs) &&
         std::equal(a.components.begin(), a.components.end(),
                    b.components.begin(), b.components.end(),
                    [](const SpellComponent &x, const SpellComponent &y) {
                      return x.position.x == y.position.x &&
                             x.position.y == y.position.y &&
                             x.scale == y.scale &&
                             x.rotationDeg == y.rotationDeg &&
                             SameGlyphs(x.glyphs, y.glyphs);
                    });
}

constexpr size_t MAX_UNDO = 200;

} // namespace

void SpellEditor::CommitEdits() {
  // Mid-drag or mid-slider the edit isn't finished yet
  if (ImGui::IsAnyItemActive() || SameSpell(m_currentSpell, m_committed))
    return;
  m_undo.push_back(m_committed);
  if (m_undo.size() > MAX_UNDO)
    m_undo.erase(m_undo.begin());
  m_redo.clear();
  m_actionBase = m_committed;
  m_committed = m_currentSpell;
}

void SpellEditor::ResetHistory() {
  m_undo.clear();
  m_redo.clear();
  m_committed = m_actionBase = m_currentSpell;
}

void SpellEditor::Undo() {
  if (m_undo.empty())
    return;
  m_redo.push_back(m_currentSpell);
  m_currentSpell = std::move(m_undo.back());
  m_undo.pop_back();
  m_committed = m_actionBase = m_currentSpell;
  ClearSelection();
  m_statusMessage = "Undone.";
}

void SpellEditor::Redo() {
  if (m_redo.empty())
    return;
  m_undo.push_back(m_currentSpell);
  m_currentSpell = std::move(m_redo.back());
  m_redo.pop_back();
  m_committed = m_actionBase = m_currentSpell;
  ClearSelection();
  m_statusMessage = "Redone.";
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
  if ((int)m_currentSpell.components.size() >= LAYER_HARD_MAX_COMPONENTS)
    return fail("A layered spell holds at most 64 spells.");
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
      ShowPaletteTab(g.kind == GlyphKind::Sign ? PaletteTab::Signs
                                               : PaletteTab::Sigils);
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
      ShowPaletteTab(PaletteTab::Spells);
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
  ImVec2 spellCenter = SpellToCanvasSpace(canvasOrigin, canvasCenter, {0, 0});
  for (const SpellGeometry::Ring &ring :
       SpellGeometry::ComponentRings(component))
    AddRing(dl, ring, spellCenter, m_zoom, color, 2.5f * ring.weight);
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
  r *= m_zoom;
  AddArrow(dl, {c.x + dir.x * r, c.y + dir.y * r}, dir, 14.0f * m_zoom,
           Theme::U32(Tone::Verdigris, 0.8f), 2.0f);
}

namespace {

// Turn by 45 degrees (-1 = back), wrapping into -180..180
void Turn(float &rotationDeg, int dir = 1) {
  rotationDeg += 45.0f * dir;
  if (rotationDeg > GLYPH_ROTATION_MAX)
    rotationDeg -= 360.0f;
  if (rotationDeg < GLYPH_ROTATION_MIN)
    rotationDeg += 360.0f;
}

// Mouse wheel over the canvas: the notches this frame
float Wheel() {
  const ImGuiIO &io = ImGui::GetIO();
  return io.MouseWheel != 0.0f ? io.MouseWheel : io.MouseWheelH;
}

// R (or Shift + wheel) turns; W / E (or the wheel) grow and shrink by
// `step` within [minV, maxV]
void Adjust(float &rotationDeg, float &scale, float step, float minV,
            float maxV, bool overCanvas) {
  if (Typing())
    return;
  if (ImGui::IsKeyPressed(ImGuiKey_R))
    Turn(rotationDeg, ImGui::GetIO().KeyShift ? -1 : 1);
  if (ImGui::IsKeyPressed(ImGuiKey_W))
    scale = std::min(scale + step, maxV);
  if (ImGui::IsKeyPressed(ImGuiKey_E))
    scale = std::max(minV, scale - step);
  float wheel = overCanvas ? Wheel() : 0.0f;
  if (wheel == 0.0f)
    return;
  if (ImGui::GetIO().KeyShift)
    Turn(rotationDeg, wheel > 0.0f ? 1 : -1);
  else
    scale = std::clamp(scale + (wheel > 0.0f ? step : -step), minV, maxV);
}

// F flips an invertible sign
void Flip(const std::string &assetId, bool &inverted) {
  if (!Typing() && ImGui::IsKeyPressed(ImGuiKey_F) &&
      SpellSystem::SignInvertible(assetId))
    inverted = !inverted;
}

} // namespace

void SpellEditor::DrawCanvas(ImVec2 canvasOrigin, ImVec2 canvasSize) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 canvasEnd = {canvasOrigin.x + canvasSize.x,
                      canvasOrigin.y + canvasSize.y};
  ImVec2 canvasCenter = {canvasSize.x * 0.5f, canvasSize.y * 0.5f};
  // The whole circle fits the canvas, whatever the window size
  m_zoom = std::clamp(std::min(canvasSize.x, canvasSize.y) /
                          (2.0f * SPELL_OUTER_RADIUS + 24.0f),
                      0.3f, 4.0f);
  ImVec2 circleCenter = {canvasOrigin.x + canvasCenter.x,
                         canvasOrigin.y + canvasCenter.y};

  dl->AddRectFilled(canvasOrigin, canvasEnd, Theme::U32(Tone::Ink));

  dl->AddCircleFilled(circleCenter, (SPELL_OUTER_RADIUS + 6.0f) * m_zoom,
                      IM_COL32(236, 226, 200, 255));
  // The spell's own rings; the embedded spells draw theirs below, in their
  // selection colours. In a layered spell the core holds the embedded
  // spells and the band around it the ring signs.
  auto rings = SpellGeometry::Rings(m_currentSpell);
  size_t ownRings = rings.size() - 2 * m_currentSpell.components.size();
  for (size_t i = 0; i < ownRings; ++i)
    AddRing(dl, rings[i], circleCenter, m_zoom, IM_COL32(34, 24, 16, 255),
            3.0f * rings[i].weight);

  for (size_t i = 0; i < m_currentSpell.components.size(); ++i) {
    const SpellComponent &component = m_currentSpell.components[i];
    bool valid = SpellGeometry::IsComponentPlacementValid(
        component, m_currentSpell, i);
    bool selected = static_cast<int>(i) == m_selectedComponent;
    ImU32 color = !valid    ? Theme::U32(Tone::Oxblood)
                  : selected ? Theme::U32(Tone::Verdigris)
                             : IM_COL32(34, 24, 16, 255);
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
      color = valid ? Theme::U32(Tone::Verdigris) : Theme::U32(Tone::Oxblood);
    } else {
      color = valid ? IM_COL32(34, 24, 16, 255) : Theme::U32(Tone::Oxblood);
    }
    DrawGlyphLines(dl, *asset, glyph, canvasOrigin, canvasCenter, color, 2.0f);
  }

  DrawVectorOverlay(dl, canvasOrigin, canvasCenter);

  ImGui::SetCursorScreenPos(canvasOrigin);
  ImGui::InvisibleButton("##spell_canvas", canvasSize);
  bool canvasHovered = ImGui::IsItemHovered();

  Vector2 spellMouse =
      CanvasToSpellSpace(canvasOrigin, canvasCenter, ImGui::GetIO().MousePos);

  m_previewSpell.reset();
  if (m_isPlacing && !m_paletteSpellRef.empty()) {
    Adjust(m_ghostRotation, m_ghostComponentScale, 0.05f, COMPONENT_SCALE_MIN,
           COMPONENT_SCALE_MAX, canvasHovered);
    if (auto ghost = GhostComponent(spellMouse); ghost && canvasHovered) {
      bool valid = CanAddComponent() && SpellGeometry::IsComponentPlacementValid(
                                            *ghost, m_currentSpell);
      DrawComponent(dl, *ghost, canvasOrigin, canvasCenter,
                    valid ? IM_COL32(34, 24, 16, 150) : Theme::U32(Tone::Oxblood, 0.7f));
      if (valid) {
        m_previewSpell = m_currentSpell;
        m_previewSpell->components.push_back(*ghost);
      }
    }
  } else if (m_isPlacing) {
    Adjust(m_ghostRotation, m_ghostScale, 0.1f, GLYPH_SCALE_MIN,
           GLYPH_SCALE_MAX, canvasHovered);
    Flip(m_paletteAssetId, m_ghostInverted);
    const SvgAsset *asset = m_library.FindById(m_paletteAssetId);
    auto ghost = GhostGlyph(spellMouse);
    if (asset && ghost && canvasHovered) {
      bool valid = IsPlacementValid(*ghost, std::nullopt);
      ImU32 ghostColor =
          valid ? IM_COL32(34, 24, 16, 180) : Theme::U32(Tone::Oxblood, 0.8f);
      DrawGlyphLines(dl, *asset, *ghost, canvasOrigin, canvasCenter,
                     ghostColor, 2.0f);
      if (valid) {
        m_previewSpell = m_currentSpell;
        m_previewSpell->glyphs.push_back(*ghost);
      }
    }
  }

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    if (m_isPlacing) {
      // A sign, sigil or component drawn onto the circle
      if (TryPlaceAt(spellMouse))
        if (AudioManager *audio = AudioManager::Instance())
          audio->PlayUi(UiSound::Draw);
    } else if (!TrySelectAt(spellMouse)) {
      ClearSelection();
    }
  }
  // Right-click lets go of whatever is picked or selected
  if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    ClearSelection();

  if (!m_isPlacing && m_selectedComponent >= 0 &&
      m_selectedComponent < (int)m_currentSpell.components.size()) {
    SpellComponent &component = m_currentSpell.components[m_selectedComponent];
    Adjust(component.rotationDeg, component.scale, 0.05f, COMPONENT_SCALE_MIN,
           COMPONENT_SCALE_MAX, canvasHovered);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      component.position = spellMouse; // turns red while out of place
  }

  if (!m_isPlacing && m_selectedGlyphIndex >= 0 &&
      m_selectedGlyphIndex < (int)m_currentSpell.glyphs.size()) {
    PlacedGlyph &glyph = m_currentSpell.glyphs[m_selectedGlyphIndex];
    Adjust(glyph.rotationDeg, glyph.scale, 0.1f, GLYPH_SCALE_MIN,
           GLYPH_SCALE_MAX, canvasHovered);
    Flip(glyph.assetId, glyph.inverted);
    // Moving it may put it out of place: it turns red until it's back
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
      glyph.position = spellMouse;
  }

  DrawHints(dl, canvasOrigin, canvasSize);
}


void SpellEditor::DrawVectorOverlay(ImDrawList *dl, ImVec2 canvasOrigin,
                                    ImVec2 canvasCenter) {
  ImVec2 center = SpellToCanvasSpace(canvasOrigin, canvasCenter, {0, 0});

  // "Up" on the circle is where the caster aims
  ImVec2 aimMark{center.x, center.y - (SPELL_OUTER_RADIUS - 6.0f) * m_zoom};
  dl->AddTriangleFilled({aimMark.x, aimMark.y - 8.0f},
                        {aimMark.x - 6.0f, aimMark.y + 4.0f},
                        {aimMark.x + 6.0f, aimMark.y + 4.0f},
                        Theme::U32(Tone::Verdigris, 0.8f));

  // Only levitation signs push; the other signs have no direction
  for (const auto &glyph : m_currentSpell.glyphs) {
    if (glyph.kind != GlyphKind::Sign || glyph.assetId != "levitation")
      continue;
    float rad = glyph.rotationDeg * DEG2RAD;
    ImVec2 dir{std::sin(rad), -std::cos(rad)};
    ImVec2 from = SpellToCanvasSpace(canvasOrigin, canvasCenter, glyph.position);
    AddArrow(dl, from, dir, 30.0f * glyph.scale * m_zoom,
             Theme::U32(Tone::Verdigris, 0.8f),
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
  AddArrow(dl, center, dir,
           std::min(SPELL_INNER_RADIUS * 0.8f, netLen * 60.0f) * m_zoom,
           color, 4.0f);
}

// Sigils, signs and saved spells, a tab each. Selecting something on the
// canvas switches to its tab.
void SpellEditor::DrawPalette() {
  if (!ImGui::BeginTabBar("Palette"))
    return;
  auto tab = [this](const char *label, PaletteTab which) {
    ImGuiTabItemFlags flags = m_switchPalette && m_paletteTab == which
                                  ? ImGuiTabItemFlags_SetSelected
                                  : 0;
    bool open = ImGui::BeginTabItem(label, nullptr, flags);
    if (open && !m_switchPalette)
      m_paletteTab = which;
    return open;
  };
  if (tab("Sigils", PaletteTab::Sigils)) {
    DrawGlyphGrid(GlyphKind::Sigil);
    ImGui::EndTabItem();
  }
  if (tab("Signs", PaletteTab::Signs)) {
    DrawGlyphGrid(GlyphKind::Sign);
    ImGui::EndTabItem();
  }
  if (tab("Spells", PaletteTab::Spells)) {
    DrawSpellPalette();
    ImGui::EndTabItem();
  }
  m_switchPalette = false;
  m_scrollToCard = false; // only this frame, whichever tab showed
  ImGui::EndTabBar();
}

void SpellEditor::DrawGlyphGrid(GlyphKind kind) {
  auto assets = m_library.GetByKind(kind);
  if (assets.empty()) {
    ImGui::TextDisabled("None available");
    return;
  }
  ImGui::BeginChild("GlyphGrid", ImVec2(0, 0), false);
  float cell = 84.0f * View::UiScale();
  int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
  ImGui::Columns(columns, nullptr, false);
  for (const SvgAsset *asset : assets) {
    if (m_allowGlyph && !m_allowGlyph(asset->id))
      continue;
    DrawAssetThumbnail(*asset, m_isPlacing && m_paletteAssetId == asset->id);
    ImGui::NextColumn();
  }
  ImGui::Columns(1);
  ImGui::EndChild();
}

// Saved single-layer spells, to embed in a layered one
void SpellEditor::DrawSpellPalette() {
  if (!m_spells)
    return;
  std::string why;
  if (!CanAddComponent(&why)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", why.c_str());
    ImGui::PopTextWrapPos();
    return;
  }

  ImGui::TextDisabled("Saved spells, to layer inside this one");
  ImGui::BeginChild("SpellPalette", ImVec2(0, 0), false);
  float ui = View::UiScale();
  float panelWidth = ImGui::GetContentRegionAvail().x;
  int columns = std::max(1, static_cast<int>(panelWidth / (84.0f * ui)));
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
    ImVec2 size(72 * ui, 72 * ui);
    if (ImGui::Selectable("##spell", selected, 0, size)) {
      ClearSelection();
      if (!selected) { // a second click puts it back
        m_paletteSpellRef = ref;
        m_isPlacing = true;
        m_ghostRotation = 0.0f;
        // The first spell fills the core; more have to share it
        m_ghostComponentScale = m_currentSpell.components.empty()
                                    ? COMPONENT_SCALE_MAX
                                    : 0.35f;
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s: click to place it inside this circle",
                        spell.name.c_str());
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, Theme::U32(Tone::Soot));
    if (m_thumbnails)
      m_thumbnails->Draw(dl, spell, {p0.x + 4, p0.y + 4},
                         {p1.x - 4, p1.y - 4});
    ImGui::PushClipRect({p0.x, p1.y}, {p1.x, p1.y + 20 * ui}, true);
    ImGui::TextUnformatted(spell.name.c_str());
    ImGui::PopClipRect();
    ImGui::PopID();
    ImGui::EndGroup();
  }
  if (shown == 0)
    ImGui::TextDisabled("Save a single-layer spell first.");
  ImGui::EndChild();
}

namespace {

// "Placing: Fire" and what fire does
void GlyphHeading(const char *verb, const std::string &assetId) {
  GlyphDocs::Info info = GlyphDocs::Get(assetId);
  ImGui::Text("%s: %s", verb, info.name ? info.name : assetId.c_str());
  if (*info.text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", info.text);
    ImGui::PopTextWrapPos();
  }
}

} // namespace

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
  if (Widgets::Button("Remove (Del)"))
    RemoveSelected();
  ImGui::SameLine();
  if (Widgets::Button("Deselect (Esc)"))
    ClearSelection();
}

void SpellEditor::DrawEditPanel() {
  if (m_isPlacing && !m_paletteSpellRef.empty()) {
    ImGui::Separator();
    ImGui::Text("Placing spell: %s", m_paletteSpellRef.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("It fires with the others, turned and shared out "
                        "by its size; the ring's signs apply to all of them.");
    ImGui::PopTextWrapPos();
    DrawClampedFloat("Scale", &m_ghostComponentScale, COMPONENT_SCALE_MIN,
                     COMPONENT_SCALE_MAX, 0.05f);
    DrawClampedFloat("Rotation", &m_ghostRotation, GLYPH_ROTATION_MIN,
                     GLYPH_ROTATION_MAX, 45.0f);
    ImGui::TextDisabled("Strength: %.0f%%",
                        SpellSystem::ComponentEffectiveness(
                            m_ghostComponentScale) *
                            100.0f);
    if (Widgets::Button("Put back (Esc)"))
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
    GlyphHeading("Placing", asset->id);

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

    if (Widgets::Button("Put back (Esc)"))
      ClearSelection();
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
  GlyphHeading("Selected", asset->id);

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

  if (Widgets::Button("Remove (Del)"))
    RemoveSelected();
  ImGui::SameLine();
  if (Widgets::Button("Deselect (Esc)"))
    ClearSelection();
}

void SpellEditor::OpenSpell(const Spell &spell) {
  m_currentSpell = spell;
  std::strncpy(m_nameBuffer, spell.name.c_str(), SPELL_NAME_MAX_LEN);
  m_nameBuffer[SPELL_NAME_MAX_LEN] = '\0';
  ClearSelection();
  ResetHistory();
  m_statusMessage.clear();
  m_tab = Tab::Edit;
  m_switchTab = true;
}

void SpellEditor::SaveCurrent() {
  bool anyInvalid = false;
  for (size_t i = 0; i < m_currentSpell.glyphs.size(); ++i)
    if (!GetAssetForGlyph(m_currentSpell.glyphs[i]) ||
        !IsPlacementValid(m_currentSpell.glyphs[i], i))
      anyInvalid = true;
  for (size_t i = 0; i < m_currentSpell.components.size(); ++i)
    if (!SpellGeometry::IsComponentPlacementValid(m_currentSpell.components[i],
                                                  m_currentSpell, i))
      anyInvalid = true;

  if (std::string problem = SpellSystem::Problem(m_currentSpell);
      !problem.empty() || !SpellSystem::Evaluate(m_currentSpell).valid) {
    m_statusMessage = problem.empty() ? "That isn't a working spell." : problem;
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

namespace {

// What an edit changed, attribute by attribute: green when it got better,
// red when worse, blue when it's neither (just different)
void DrawStatChanges(const Spell &beforeSpell, const Spell &afterSpell) {
  SpellStats a = SpellSystem::Evaluate(beforeSpell);
  SpellStats b = SpellSystem::Evaluate(afterSpell);
  const ImVec4 good = Theme::Vec(Tone::Moss);
  const ImVec4 bad = Theme::Vec(Tone::Oxblood);
  const ImVec4 neutral = Theme::Vec(Tone::Verdigris);
  if (a.valid != b.valid)
    ImGui::TextColored(b.valid ? good : bad, "%s",
                       b.valid ? "Now a working spell"
                               : "No longer a working spell");
  if (!a.valid || !b.valid)
    return;

  struct Row {
    const char *label;
    float before, after;
    int better; // +1 higher is better, -1 lower is better, 0 neither
    const char *fmt;
  };
  std::vector<Row> rows;
  auto add = [&rows](const char *label, float x, float y, int better,
                     const char *fmt) {
    if (std::abs(x - y) > 1e-3f * std::max(1.0f, std::abs(x)))
      rows.push_back({label, x, y, better, fmt});
  };
  auto cast = [](const SpellStats &s) {
    return TurnController::CastTicks(s) * TurnController::TICK_DT;
  };
  add("Balance %", (1 - a.imbalance) * 100, (1 - b.imbalance) * 100, 1,
      "%.0f");
  add("Cast time", cast(a), cast(b), -1, "%.2fs");
  if (a.kind == SpellKind::Compound || b.kind == SpellKind::Compound) {
    add("Parts", (float)a.parts.size(), (float)b.parts.size(), 0, "%.0f");
  } else {
    add("Speed", a.speed, b.speed, 1, "%.0f");
    add("Range", a.range, b.range, 1, "%.0f");
    add("Density", a.density, b.density, 1, "%.2f");
    add("Power", a.power, b.power, 1, "%.0f");
    add("Diameter", a.diameter, b.diameter, 0, "%.1f");
    add("Particles", (float)a.particleCount, (float)b.particleCount, 1, "%.0f");
    add("Heat", a.temperature, b.temperature, 0, "%.0f");
    add("Hardness x", a.hardnessScale, b.hardnessScale, 1, "%.2f");
    add("Launch", a.launchSpeed, b.launchSpeed, 1, "%.0f");
    add("Force", a.force, b.force, 1, "%.0f");
    add("Duration", a.duration, b.duration, 1, "%.2fs");
    add("Collects", (float)a.collectMax, (float)b.collectMax, 1, "%.0f");
    add("Flash radius", a.flashRadius, b.flashRadius, 1, "%.0f");
    add("Blinds for", a.flashTime, b.flashTime, 1, "%.1fs");
    add("Guided turn", a.homeTurnRate * RAD2DEG, b.homeTurnRate * RAD2DEG, 1,
        "%.0f deg/s");
    add("Follows cursor", a.steerTime, b.steerTime, 1, "%.1fs");
    add("Crush", std::max(0.0f, a.crush), std::max(0.0f, b.crush), 1, "%.1f");
    add("Reform", std::max(0.0f, -a.crush), std::max(0.0f, -b.crush), 1,
        "%.1f");
    add("Restore", a.restore, b.restore, 1, "%.1f");
    add("Cooling", -a.temperatureDelta, -b.temperatureDelta, 0, "%.0f C");
    add("Held for", a.holdTime, b.holdTime, 1, "%.1fs");
  }
  if (rows.empty())
    return;
  if (!ImGui::BeginTable("changes", 2, ImGuiTableFlags_SizingStretchProp))
    return;
  for (const Row &r : rows) {
    float d = r.after - r.before;
    ImVec4 color = r.better == 0 ? neutral : (d * r.better > 0 ? good : bad);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(color, "%s %s", d > 0 ? "+" : "-", r.label);
    ImGui::TableNextColumn();
    std::string fmt = std::string(r.fmt) + " -> " + r.fmt;
    ImGui::TextColored(color, fmt.c_str(), r.before, r.after);
  }
  ImGui::EndTable();
  ImGui::Separator();
}

} // namespace

void SpellEditor::DrawOverlay() {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(
      {(float)GetScreenWidth(), (float)GetScreenHeight()});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Vec(Tone::Ink, 0.97f));
  ImGui::Begin("SpellEditorOverlay", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  // Close sits at the right of the tab row
  float closeW = ImGui::CalcTextSize("Close (Esc)").x +
                 ImGui::GetStyle().FramePadding.x * 2;
  ImVec2 row = ImGui::GetCursorPos();
  ImGui::SetCursorPos({ImGui::GetWindowWidth() - closeW - 10, row.y});
  // Esc lets go of what's picked or selected first, then closes
  bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape) &&
                !ImGui::IsAnyItemActive() && !m_openPopup;
  bool holding = m_isPlacing || m_selectedGlyphIndex >= 0 ||
                 m_selectedComponent >= 0;
  if (escape && holding && m_tab == Tab::Edit) {
    ClearSelection();
  } else if (Widgets::Button("Close (Esc)") || escape) {
    m_open = false;
    ClearSelection();
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
  if (Widgets::Button("Save spell"))
    SaveCurrent();
  ImGui::SameLine();
  if (Widgets::Button("New")) {
    m_currentSpell = {};
    m_nameBuffer[0] = '\0';
    ClearSelection();
    ResetHistory();
    m_statusMessage.clear();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(m_undo.empty());
  if (Widgets::Button("Undo"))
    Undo();
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Ctrl+Z");
  ImGui::SameLine();
  ImGui::BeginDisabled(m_redo.empty());
  if (Widgets::Button("Redo"))
    Redo();
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Ctrl+Y / Ctrl+Shift+Z");
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_spells || !m_spells->Find(m_spells->RefOf(m_nameBuffer)));
  if (Widgets::Button("Test in sandbox"))
    m_testRef = m_spells->RefOf(m_nameBuffer);
  ImGui::EndDisabled();
  if (!m_statusMessage.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(Theme::Vec(Tone::Moss), "%s",
                       m_statusMessage.c_str());
  }
  DrawGlyphCounter();

  ImGui::Separator();
  if (SpellRules::CountGlyphs(m_currentSpell).OverLimit())
    ImGui::TextColored(Theme::Vec(Tone::Oxblood),
                       "Over the limit (more than %d signs in a circle or %d "
                       "spells in a layered spell): this spell can only be "
                       "played solo or in chaos rooms.",
                       SIGN_LIMIT, LAYER_MAX_COMPONENTS);

  float rightW = std::max(280.0f * View::UiScale(),
                          ImGui::GetContentRegionAvail().x * 0.24f);
  float leftW = ImGui::GetContentRegionAvail().x - rightW - 8.0f;
  const float stripH = 64.0f * View::UiScale();

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
  if (ImGui::CollapsingHeader("Spell Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Hovering a spot to place something shows the spell with it placed;
    // otherwise the changes are those of the edit in progress, or the last
    const Spell &after = m_previewSpell ? *m_previewSpell : m_currentSpell;
    const Spell &before = m_previewSpell ? m_currentSpell
                          : SameSpell(m_currentSpell, m_committed)
                              ? m_actionBase
                              : m_committed;
    if (m_previewSpell)
      ImGui::TextDisabled("If placed here:");
    DrawStatChanges(before, after);
    ImGui::PushTextWrapPos(0.0f);
    DrawStats(SpellSystem::Evaluate(after),
              SpellSystem::Problem(after).c_str());
    ImGui::PopTextWrapPos();
  }
  DrawEditPanel();
  DrawPalette();
  ImGui::EndChild();

  HandleShortcuts();
  CommitEdits();
  ImGui::End();
  ImGui::PopStyleColor();
}

// "Signs 12/32  Sigils 2" at the right of the top row; the sign count is
// the fullest circle's, since the limit is per circle
void SpellEditor::DrawGlyphCounter() const {
  SpellRules::Count count = SpellRules::CountGlyphs(m_currentSpell);
  bool over = count.OverLimit();
  char text[96];
  int n = std::snprintf(text, sizeof text, "Signs %d/%d    Sigils %d",
                        count.mostSigns, SIGN_LIMIT, count.sigils);
  if (count.parts > 0)
    std::snprintf(text + n, sizeof text - n, "    Spells %d/%d", count.parts,
                  LAYER_MAX_COMPONENTS);
  float w = ImGui::CalcTextSize(text).x;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                           ImGui::GetWindowContentRegionMax().x - w -
                               ImGui::GetStyle().ItemSpacing.x));
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(Theme::Vec(over ? Tone::Oxblood
                                : count.mostSigns > SIGN_LIMIT * 3 / 4
                                    ? Tone::Brass
                                    : Tone::Muted),
                     "%s", text);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Ordinary matches allow %d signs per circle and %d spells "
                "in a layered spell.",
                SIGN_LIMIT, LAYER_MAX_COMPONENTS);
    ImGui::TextDisabled("Sigils follow the usual rules. Chaos rooms and solo "
                        "play have no limit.");
    if (count.circles.size() > 1)
      for (size_t i = 0; i < count.circles.size(); ++i)
        ImGui::Text("%s: %d signs, %d sigils",
                    i == 0 ? "Outer ring" : TextFormat("Part %d", (int)i),
                    count.circles[i].signs, count.circles[i].sigils);
    ImGui::EndTooltip();
  }
}

void SpellEditor::Draw() {
  if (!m_open)
    return;
  DrawOverlay();
}
