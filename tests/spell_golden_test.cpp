#include "whas/spell/spell_quant.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

// Golden spell vectors shared with the C# server (server/Whas.Server.Tests):
// both must turn the same glyphs into exactly these quantized stats.
// Regenerate with: whas_tests "[.generate]" (after a deliberate tuning change,
// bumping SpellQuant::EVALUATOR_VERSION).

namespace {

constexpr const char *FIXTURE = WHAS_SOURCE_DIR "/tests/fixtures/spells.json";

nlohmann::json GlyphJson(const PlacedGlyph &g) {
  return {{"assetId", g.assetId},
          {"kind", g.kind == GlyphKind::Sigil ? "sigil" : "sign"},
          {"x", g.position.x},
          {"y", g.position.y},
          {"scale", g.scale},
          {"rotation", g.rotationDeg}};
}

Spell FromJson(const nlohmann::json &j) {
  Spell s;
  s.name = j.at("name").get<std::string>();
  for (const auto &g : j.at("glyphs")) {
    PlacedGlyph pg;
    pg.assetId = g.at("assetId").get<std::string>();
    pg.kind = g.at("kind") == "sigil" ? GlyphKind::Sigil : GlyphKind::Sign;
    pg.position = {g.at("x").get<float>(), g.at("y").get<float>()};
    pg.scale = g.at("scale").get<float>();
    pg.rotationDeg = g.at("rotation").get<float>();
    s.glyphs.push_back(pg);
  }
  return s;
}

std::vector<Spell> Cases() {
  const char *sigils[] = {"water", "fire", "earth", "ice", "sand",
                          "rock",  "wind", "gust",  "bogus"};
  const float sigilScales[] = {0.1f, 0.5f, 1.0f, 1.3f, 2.2f, 3.0f};
  const float rotations[] = {-180, -135, -90, -45, 0, 45, 90, 135, 180};
  std::vector<Spell> out;
  int n = 0;
  for (const char *sigil : sigils) {
    for (float sigilScale : sigilScales) {
      for (int signs = 0; signs <= 5; ++signs) {
        Spell s;
        s.name = "case" + std::to_string(n++);
        s.glyphs.push_back(
            {sigil, GlyphKind::Sigil, {0, 0}, sigilScale, 0.0f});
        for (int i = 0; i < signs; ++i) {
          // A spread of rotations and scales, some balanced, some not
          float rot = rotations[(n * 7 + i * 5) % 9];
          float scale = 0.1f * static_cast<float>(1 + (n * 3 + i * 11) % 30);
          s.glyphs.push_back(
              {"column", GlyphKind::Sign, {10.0f * i, -120.0f}, scale, rot});
        }
        out.push_back(std::move(s));
      }
    }
  }
  // No sigil, and two sigils: both invalid
  out.push_back({"no-sigil", {{"column", GlyphKind::Sign, {0, 0}, 1, 0}}});
  out.push_back({"two-sigils",
                 {{"fire", GlyphKind::Sigil, {0, 0}, 1, 0},
                  {"water", GlyphKind::Sigil, {40, 0}, 1, 0}}});
  return out;
}

} // namespace

TEST_CASE("write golden spell vectors", "[.generate]") {
  nlohmann::json cases = nlohmann::json::array();
  for (const Spell &s : Cases()) {
    nlohmann::json glyphs = nlohmann::json::array();
    for (const PlacedGlyph &g : s.glyphs)
      glyphs.push_back(GlyphJson(g));
    cases.push_back({{"name", s.name},
                     {"glyphs", glyphs},
                     {"stats", SpellQuant::Quantize(SpellSystem::Evaluate(s))}});
  }
  std::ofstream(FIXTURE) << nlohmann::json{
      {"evaluatorVersion", SpellQuant::EVALUATOR_VERSION},
      {"cases", cases}}.dump(1);
}

TEST_CASE("spells evaluate to the golden quantized stats", "[spell]") {
  std::ifstream file(FIXTURE);
  REQUIRE(file);
  nlohmann::json j = nlohmann::json::parse(file);
  REQUIRE(j.at("evaluatorVersion") == SpellQuant::EVALUATOR_VERSION);
  REQUIRE(j.at("cases").size() > 300);
  for (const auto &c : j.at("cases")) {
    Spell spell = FromJson(c);
    INFO(spell.name);
    REQUIRE(SpellQuant::Quantize(SpellSystem::Evaluate(spell)) ==
            c.at("stats").get<SpellQuant::Stats>());
  }
}
