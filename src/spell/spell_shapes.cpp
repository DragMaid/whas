#include "whas/spell/spell_shapes.h"
#include <algorithm>
#include <cmath>

// The spell shapes. Changing a trigger changes spell stats: bump
// SpellQuant::EVALUATOR_VERSION, mirror it in SpellEvaluator.cs and
// regenerate the golden spells. Changing a ShapeDef changes the simulation:
// re-record tests/replays/bot_match.json.

namespace {

const std::vector<ShapeTrigger> kTriggers = {
    {SpellShape::Orb, "orb", GlyphKind::Sign, 1.0f, 0.0f},
    {SpellShape::Dragon, "dragon", GlyphKind::Sigil, 1.0f, 0.0f},
};

using Kind = ShapePart::Kind;

ShapePart BeamRows() {
  ShapePart p;
  p.kind = Kind::Rows;
  p.beamLanes = true;
  return p;
}

std::vector<ShapeDef> BuildShapes() {
  std::vector<ShapeDef> shapes;

  shapes.push_back({SpellShape::Stream, "stream", 0.0f, 1.0f, {BeamRows()}});

  {
    // A ball gathered just ahead of the caster, then thrown
    ShapePart ball;
    ball.kind = Kind::Burst;
    ball.disk = true;
    shapes.push_back({SpellShape::Orb, "orb", 0.0f, 1.0f, {ball, BeamRows()}});
  }

  {
    // Snout, open jaws, the skull with an eye (the gap), horns sweeping
    // back, then the neck; half a cell between columns
    ShapePart head;
    head.kind = Kind::Burst;
    head.columnSpacing = 0.5f;
    head.rowSpacing = 1.3f;
    head.scaleDiameter = 6.0f;
    head.art = {
        "..........#.#..........",
        "........#.....#........",
        ".......#.......#.......",
        ".......#..#.#..#.......",
        ".......#.#.#.#.#.......",
        "......#.#...#.#.#......",
        "......#.#.#.#.#.#......",
        "....#...#.#.#.#...#....",
        "..#......#.#.#......#..",
        "#........#.#.#........#",
        ".........#.#.#.........",
        "..........#.#..........",
    };

    ShapePart body;
    body.art = {"##"};
    body.scaleDiameter = 6.0f;
    body.length = 28;

    // Thinning to a single line that ends in a forked fin
    ShapePart tail;
    tail.columnSpacing = 0.5f;
    tail.scaleDiameter = 6.0f;
    tail.art = {
        ".#.#.", ".#.#.", ".#.#.", ".#.#.", "..#..", "..#..",
        "..#..", "..#..", "..#..", "..#..", "..#..", "..#..",
        "..#..", ".#.#.", "#...#", "#...#",
    };
    tail.length = static_cast<int>(tail.art.size());

    shapes.push_back(
        {SpellShape::Dragon, "dragon", 2.5f, 22.0f, {head, body, tail}});
  }

  for (ShapeDef &def : shapes)
    for (ShapePart &part : def.parts)
      for (const std::string &line : part.art)
        part.width = std::max(part.width, line.size());
  std::sort(shapes.begin(), shapes.end(),
            [](const ShapeDef &a, const ShapeDef &b) {
              return a.shape < b.shape;
            });
  return shapes;
}

} // namespace

namespace SpellShapes {

const std::vector<ShapeTrigger> &Triggers() { return kTriggers; }

const ShapeTrigger *TriggerFor(const std::string &glyph, GlyphKind kind) {
  for (const ShapeTrigger &t : kTriggers)
    if (t.kind == kind && glyph == t.glyph)
      return &t;
  return nullptr;
}

const ShapeDef &Get(SpellShape shape) {
  static const std::vector<ShapeDef> shapes = BuildShapes();
  size_t i = static_cast<size_t>(shape);
  return i < shapes.size() ? shapes[i] : shapes[0];
}

float PartScale(const ShapePart &part, float diameter) {
  if (part.scaleDiameter <= 0.0f)
    return 1.0f;
  return std::clamp(diameter / part.scaleDiameter, part.minScale,
                    part.maxScale);
}

int ScaledRows(const ShapePart &part, float scale) {
  if (part.art.empty())
    return 0;
  return std::max(
      1, static_cast<int>(std::lround(part.art.size() * scale)));
}

std::vector<float> RowOffsets(const ShapePart &part, int row, float scale) {
  std::vector<float> out;
  if (part.art.empty() || part.width == 0)
    return out;
  // Each drawn row and column samples the art cell it falls in
  int rows = static_cast<int>(part.art.size());
  int src = std::min(rows - 1, static_cast<int>((row + 0.5f) / scale));
  const std::string &line = part.art[std::max(0, src)];
  int cols = std::max(1, static_cast<int>(std::lround(part.width * scale)));
  float centre = (cols - 1) * 0.5f;
  for (int m = 0; m < cols; ++m) {
    size_t c = std::min(part.width - 1,
                        static_cast<size_t>((m + 0.5f) / scale));
    if (c < line.size() && line[c] == '#')
      out.push_back((m - centre) * part.columnSpacing);
  }
  return out;
}

int PartLength(const ShapePart &part, float scale) {
  if (part.length <= 0)
    return 0;
  return std::max(1, static_cast<int>(std::lround(part.length * scale)));
}

int FigureRows(const ShapeDef &shape, float diameter) {
  int rows = 0;
  for (const ShapePart &part : shape.parts) {
    if (part.kind != ShapePart::Kind::Rows)
      continue;
    int length = PartLength(part, PartScale(part, diameter));
    if (part.beamLanes || length == 0)
      return -1;
    rows += length;
  }
  return rows;
}

int MaterialNeeded(const ShapeDef &shape, float diameter) {
  int total = 0;
  for (const ShapePart &part : shape.parts) {
    if (part.disk || part.beamLanes)
      return -1;
    float scale = PartScale(part, diameter);
    int rows = part.kind == ShapePart::Kind::Burst ? ScaledRows(part, scale)
                                                   : PartLength(part, scale);
    if (rows == 0)
      return -1; // open-ended
    int cycle = ScaledRows(part, scale);
    for (int r = 0; r < rows; ++r)
      total += static_cast<int>(RowOffsets(part, r % cycle, scale).size());
  }
  return total;
}

} // namespace SpellShapes
