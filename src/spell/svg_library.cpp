#include "whas/spell/svg_library.h"
#include "nanosvg.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <cmath>

namespace {

bool IsDarkStroke(unsigned int color) {
  int r = (color >> 0) & 0xFF;
  int g = (color >> 8) & 0xFF;
  int b = (color >> 16) & 0xFF;
  return (r + g + b) < 600;
}

float Distance(Vector2 a, Vector2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// How far a point is from the line through a and b
float OffLine(Vector2 p, Vector2 a, Vector2 b) {
  float len = Distance(a, b);
  if (len < 1e-4f)
    return Distance(p, a);
  return std::abs((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / len;
}

// nanosvg gives every path as cubic Béziers (lines and circles too). A
// straight one becomes one segment; a curve gets more segments the longer
// it is, so small circles (a head, a dot) keep their shape.
void FlattenPath(const NSVGpath *path, std::vector<LineSeg> &out) {
  if (!path || path->npts < 4)
    return;

  for (int i = 0; i < path->npts - 1; i += 3) {
    float *p = &path->pts[i * 2];
    Vector2 p0{p[0], p[1]};
    Vector2 p1{p[2], p[3]};
    Vector2 p2{p[4], p[5]};
    Vector2 p3{p[6], p[7]};

    if (OffLine(p1, p0, p3) < 0.05f && OffLine(p2, p0, p3) < 0.05f) {
      if (Distance(p0, p3) > 1e-3f)
        out.push_back({p0, p3});
      continue;
    }

    // The control polygon is never shorter than the curve
    float length = Distance(p0, p1) + Distance(p1, p2) + Distance(p2, p3);
    int segments = std::clamp(static_cast<int>(std::ceil(length / 1.5f)), 4, 24);
    Vector2 prev = p0;
    for (int j = 1; j <= segments; ++j) {
      float t = static_cast<float>(j) / segments;
      float mt = 1.0f - t;
      float mt2 = mt * mt;
      float mt3 = mt2 * mt;
      float t2 = t * t;
      float t3 = t2 * t;

      Vector2 current;
      current.x = mt3 * p0.x + 3.0f * mt2 * t * p1.x + 3.0f * mt * t2 * p2.x + t3 * p3.x;
      current.y = mt3 * p0.y + 3.0f * mt2 * t * p1.y + 3.0f * mt * t2 * p2.y + t3 * p3.y;

      // Tiny steps are merged into the next one, never dropped (dropping
      // them used to leave gaps, and erase small circles entirely)
      if (Distance(prev, current) > 0.2f || j == segments) {
        out.push_back({prev, current});
        prev = current;
      }
    }
  }
  // A closed path ends where it started
  if (path->closed) {
    Vector2 first{path->pts[0], path->pts[1]};
    Vector2 last{path->pts[(path->npts - 1) * 2],
                 path->pts[(path->npts - 1) * 2 + 1]};
    if (Distance(first, last) > 1e-3f)
      out.push_back({last, first});
  }
}

void ParseLineElements(const std::string &content, std::vector<LineSeg> &out) {
  static const std::regex lineRe(
      "<line[^>]*x1\\s*=\\s*\"([0-9.+-]+)\"[^>]*y1\\s*=\\s*\"([0-9.+-]+)\"[^>]*x2\\s*=\\s*\"([0-9.+-]+)\"[^>]*y2\\s*=\\s*\"([0-9.+-]+)\"",
      std::regex::icase);
  auto begin = std::sregex_iterator(content.begin(), content.end(), lineRe);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    float x1 = std::strtof((*it)[1].str().c_str(), nullptr);
    float y1 = std::strtof((*it)[2].str().c_str(), nullptr);
    float x2 = std::strtof((*it)[3].str().c_str(), nullptr);
    float y2 = std::strtof((*it)[4].str().c_str(), nullptr);
    out.push_back({{x1, y1}, {x2, y2}});
  }
}

} // namespace

void SvgLibrary::LoadFromDirectories(const std::string &signsDir,
                                     const std::string &sigilsDir) {
  m_assets.clear();

  auto scan = [this](const std::string &dir, GlyphKind kind) {
    std::filesystem::path p(dir);
    if (!std::filesystem::exists(p))
      return;
    std::vector<std::filesystem::path> inverted;
    for (const auto &entry : std::filesystem::directory_iterator(p)) {
      if (!entry.is_regular_file())
        continue;
      if (entry.path().extension() != ".svg")
        continue;
      // "<id>.inverted.svg" is another drawing of <id>, not its own glyph
      if (entry.path().stem().extension() == ".inverted") {
        inverted.push_back(entry.path());
        continue;
      }
      LoadSvgFile(entry.path().string(), kind);
    }
    for (const auto &path : inverted) {
      std::string id = path.stem().stem().string();
      for (auto &asset : m_assets) {
        if (asset.id != id)
          continue;
        SvgAsset shape;
        if (ParseShape(path.string(), shape))
          asset.invertedSegments = std::move(shape.segments);
      }
    }
  };

  scan(signsDir, GlyphKind::Sign);
  scan(sigilsDir, GlyphKind::Sigil);

  // Without a drawing of its own, an inverted glyph is turned 180 degrees
  for (auto &asset : m_assets) {
    if (!asset.invertedSegments.empty())
      continue;
    Vector2 c = asset.localCenter;
    for (const LineSeg &seg : asset.segments)
      asset.invertedSegments.push_back(
          {{2 * c.x - seg.a.x, 2 * c.y - seg.a.y},
           {2 * c.x - seg.b.x, 2 * c.y - seg.b.y}});
  }

  std::sort(m_assets.begin(), m_assets.end(),
            [](const SvgAsset &a, const SvgAsset &b) { return a.id < b.id; });
}

// TODO: doing a O(n) loop might not be a very smart move for this
const SvgAsset *SvgLibrary::FindById(const std::string &id) const {
  for (const auto &a : m_assets) {
    if (a.id == id)
      return &a;
  }
  return nullptr;
}

std::vector<const SvgAsset *> SvgLibrary::GetByKind(GlyphKind kind) const {
  std::vector<const SvgAsset *> out;
  for (const auto &a : m_assets) {
    if (a.kind == kind)
      out.push_back(&a);
  }
  return out;
}

bool SvgLibrary::LoadSvgFile(const std::string &path, GlyphKind kind) {
  SvgAsset asset;
  asset.id = std::filesystem::path(path).stem().string();
  asset.kind = kind;
  asset.path = path;
  if (!ParseShape(path, asset))
    return false;
  m_assets.push_back(std::move(asset));
  return true;
}

bool SvgLibrary::ParseShape(const std::string &path, SvgAsset &asset) {
  std::ifstream file(path);
  if (!file)
    return false;

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  std::vector<char> mutableContent(content.begin(), content.end());
  mutableContent.push_back('\0');
  NSVGimage *image = nsvgParse(mutableContent.data(), "px", 96.0f);

  if (image) {
    asset.viewWidth = image->width > 0 ? image->width : 64.0f;
    asset.viewHeight = image->height > 0 ? image->height : 64.0f;
    asset.localCenter = {asset.viewWidth * 0.5f, asset.viewHeight * 0.5f};

    for (NSVGshape *shape = image->shapes; shape; shape = shape->next) {
      if (shape->fill.type == NSVG_PAINT_COLOR &&
          !IsDarkStroke(shape->fill.color))
        continue;

      bool hasStroke = shape->stroke.type == NSVG_PAINT_COLOR &&
                       IsDarkStroke(shape->stroke.color);
      bool hasDarkFill = shape->fill.type == NSVG_PAINT_COLOR &&
                         IsDarkStroke(shape->fill.color);

      if (!hasStroke && !hasDarkFill)
        continue;

      for (NSVGpath *pathNode = shape->paths; pathNode;
           pathNode = pathNode->next)
        FlattenPath(pathNode, asset.segments);
    }

    nsvgDelete(image);
  } else {
    asset.viewWidth = 64.0f;
    asset.viewHeight = 64.0f;
    asset.localCenter = {32.0f, 32.0f};
  }

  if (asset.segments.empty())
    ParseLineElements(content, asset.segments);

  if (asset.segments.empty())
    return false;

  float scaleX = 64.0f / asset.viewWidth;
  float scaleY = 64.0f / asset.viewHeight;
  float scale = std::min(scaleX, scaleY);

  for (auto &seg : asset.segments) {
    seg.a.x *= scale;
    seg.a.y *= scale;
    seg.b.x *= scale;
    seg.b.y *= scale;
  }

  asset.viewWidth *= scale;
  asset.viewHeight *= scale;
  asset.localCenter = {asset.viewWidth * 0.5f, asset.viewHeight * 0.5f};
  return true;
}
