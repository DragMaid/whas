#pragma once
#include "whas/spell/spell_types.h"
#include <cstdint>
#include <string>
#include <vector>

// How an element spell's particles leave the caster. Every shape is data in
// src/spell/spell_shapes.cpp, played by one emitter (SpellSystem). To add
// one:
//   1. a value here (before Count), and the same in SpellShape in
//      server/Whas.Server/Spells/SpellModel.cs
//   2. its ShapeDef in spell_shapes.cpp: parts with ASCII art
//   3. the glyph that picks it: an SVG in assets/signs or assets/sigils, a
//      ShapeTrigger in spell_shapes.cpp and in SpellEvaluator.cs, the glyph in
//      SpellValidator.cs's known signs or sigils
//   4. bump SpellQuant::EVALUATOR_VERSION with SpellEvaluator.Version and
//      regenerate the golden spells (whas_tests "[.generate]")
enum class SpellShape : uint8_t {
  Stream, // a beam, lane by lane (no glyph needed)
  Orb,    // all at once, packed into a ball (orb sign)
  Dragon, // a horned head, then a long weaving body (dragon sigil)

  Count
};

// Which glyph picks a shape. A shape doesn't make material: the element
// sigil gives it and collection adds more; a figure that needs more than it
// has is cut short at its end. The multiplier (particle count x
// (particlesBase + particlesPerScale * summed glyph scale)) is there for a
// shape that should gather some itself; both current ones leave it at 1.
// Part of the spell stats, so server/Whas.Server/Spells/SpellEvaluator.cs
// has the same table. When several are present their multipliers stack in
// table order and the last one listed decides the shape.
struct ShapeTrigger {
  SpellShape shape;
  const char *glyph;
  GlyphKind kind; // a sigil trigger is a shape sigil: it needs an element
                  // sigil beside it
  float particlesBase;
  float particlesPerScale;
};

// One stage of a shape. The emitter runs the parts in order.
struct ShapePart {
  enum class Kind : uint8_t {
    Burst, // the whole pattern at once (one tick), laid out ahead of the
           // caster on the aim line, so it flies where it's aimed
    Rows,  // `length` rows streamed out behind, one cell apart
  };
  Kind kind = Kind::Rows;

  // The pattern as ASCII art, front row first: '#' is a particle, anything
  // else a gap. Columns are `columnSpacing` cells apart, centred on the aim
  // line; Burst rows are `rowSpacing` cells apart. Rows parts cycle through
  // their rows. Drawn bigger or smaller the art is resampled (nearest
  // neighbour), so it stays solid at any size.
  std::vector<std::string> art;
  float columnSpacing = 1.0f;
  float rowSpacing = 1.0f;

  bool beamLanes = false; // Rows: a row across the whole beam, not the art
  bool disk = false;      // Burst: a round ball of every particle left

  // The art grows with the beam (and so with the sigil and expansion
  // signs): scale = diameter / scaleDiameter, clamped to [minScale,
  // maxScale]. 0 keeps it at 1.
  float scaleDiameter = 0.0f;
  float minScale = 0.5f;
  float maxScale = 3.0f;

  // Rows: how many rows (at scale 1) before the next part; the art cycles.
  // 0 = until the material runs out.
  int length = 0;

  size_t width = 0; // widest art row, filled in by the registry
};

struct ShapeDef {
  SpellShape shape;
  const char *name;
  // Rows parts weave side to side: cells each way, rows per full weave;
  // 0 = straight
  float weaveAmplitude = 0.0f;
  float weaveWavelength = 1.0f;
  std::vector<ShapePart> parts;
};

namespace SpellShapes {

const std::vector<ShapeTrigger> &Triggers();
// The trigger for a glyph, or nullptr when it doesn't pick a shape
const ShapeTrigger *TriggerFor(const std::string &glyph, GlyphKind kind);

const ShapeDef &Get(SpellShape shape);

// How big a part is drawn for a beam of this diameter
float PartScale(const ShapePart &part, float diameter);
// Rows of the part's art at that scale
int ScaledRows(const ShapePart &part, float scale);
// Offsets (cells across the aim line, left to right) of the particles in
// row `row` (0 = front) of the art drawn at `scale`
std::vector<float> RowOffsets(const ShapePart &part, int row, float scale);
// Rows a Rows part streams at that scale (0 = until the material runs out)
int PartLength(const ShapePart &part, float scale);

// Rows the Rows parts stream in all (a set figure's length in cells), or -1
// when one of them runs until the material does
int FigureRows(const ShapeDef &shape, float diameter);

// Particles the whole figure takes for a beam of this diameter, or -1 for
// a shape that just uses whatever it has (a stream, an orb)
int MaterialNeeded(const ShapeDef &shape, float diameter);

} // namespace SpellShapes
