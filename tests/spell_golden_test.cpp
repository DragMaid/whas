#include "whas/spell/spell_json.h"
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

Spell FromJson(const nlohmann::json &j) {
  Spell s;
  s.name = j.at("name").get<std::string>();
  SpellJson::Read(j, s);
  return s;
}

std::vector<Spell> Cases() {
  const char *sigils[] = {"water", "fire", "earth", "ice", "sand",
                          "rock",  "wind_underfoot", "wind",  "light", "bogus"};
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
              {"levitation", GlyphKind::Sign, {10.0f * i, -120.0f}, scale, rot});
        }
        out.push_back(std::move(s));
      }
    }
  }
  // Columns without levitation stand still; with shapes and repetition
  for (const char *sigil : {"water", "earth", "fire", "sand"})
    for (float scale : {0.4f, 1.0f, 2.2f}) {
      Spell s;
      s.name = "case" + std::to_string(n++);
      s.glyphs = {{sigil, GlyphKind::Sigil, {0, 0}, scale, 0.0f},
                  {"column", GlyphKind::Sign, {0, -120}, scale, 0.0f}};
      out.push_back(s);
      s.name = "case" + std::to_string(n++);
      s.glyphs.push_back({"orb", GlyphKind::Sign, {100, 0}, 1.0f, 0.0f});
      s.glyphs.push_back({"repetition", GlyphKind::Sign, {-100, 0}, 1.0f, 0.0f});
      out.push_back(std::move(s));
    }
  // No sigil, and two sigils: both invalid
  out.push_back({"no-sigil", {{"levitation", GlyphKind::Sign, {0, 0}, 1, 0}}});
  out.push_back({"two-sigils",
                 {{"fire", GlyphKind::Sigil, {0, 0}, 1, 0},
                  {"water", GlyphKind::Sigil, {40, 0}, 1, 0}}});

  // Modifier signs, alone and doubled, plain and inverted
  const char *modifiers[] = {"convergence", "crushing",      "repetition",
                             "cooling",     "strengthening", "collection",
                             "expansion",   "orb",           "pulling",
                             "sights_set",  "column"};
  const char *modSigils[] = {"water", "fire",           "earth",
                             "rock",  "wind",           "wind_underfoot",
                             "light"};
  const float modScales[] = {0.3f, 1.0f, 2.5f};
  for (const char *mod : modifiers) {
    for (const char *sigil : modSigils) {
      for (float scale : modScales) {
        for (int inverted = 0; inverted <= 1; ++inverted) {
          if (inverted && !SpellSystem::SignInvertible(mod))
            continue;
          Spell s;
          s.name = "case" + std::to_string(n++);
          s.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, 1.1f, 0.0f});
          s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 1.0f,
                              0.0f});
          s.glyphs.push_back({mod, GlyphKind::Sign, {-100, 60}, scale, 45.0f,
                              inverted == 1});
          if (scale > 2.0f)
            s.glyphs.push_back({mod, GlyphKind::Sign, {100, 60}, 0.7f, 0.0f});
          out.push_back(std::move(s));
        }
      }
    }
  }

  // Everything at once
  {
    Spell s;
    s.name = "kitchen-sink";
    s.glyphs.push_back({"fire", GlyphKind::Sigil, {0, 0}, 1.4f, 0.0f});
    float x = -150;
    for (const char *mod : modifiers) {
      s.glyphs.push_back({mod, GlyphKind::Sign, {x, 80}, 0.6f, 0.0f});
      x += 40;
    }
    s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 1.5f, -45.0f});
    out.push_back(std::move(s));
  }

  // Dragon: a shape sigil that needs an element sigil beside it
  for (const char *sigil : {"water", "fire", "earth", "wind", "wind_underfoot"}) {
    for (float scale : {0.5f, 1.5f}) {
      Spell s;
      s.name = "case" + std::to_string(n++);
      s.glyphs.push_back({sigil, GlyphKind::Sigil, {0, 0}, scale, 0.0f});
      s.glyphs.push_back({"dragon", GlyphKind::Sigil, {0, 80}, 1.0f, 0.0f});
      s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 1.2f, 0.0f});
      if (scale > 1.0f)
        s.glyphs.push_back({"orb", GlyphKind::Sign, {90, 0}, 1.0f, 0.0f});
      out.push_back(std::move(s));
    }
  }
  out.push_back({"dragon-alone",
                 {{"dragon", GlyphKind::Sigil, {0, 0}, 1, 0},
                  {"levitation", GlyphKind::Sign, {0, -120}, 1, 0}}});
  out.push_back({"two-dragons",
                 {{"water", GlyphKind::Sigil, {0, 0}, 1, 0},
                  {"dragon", GlyphKind::Sigil, {60, 0}, 1, 0},
                  {"dragon", GlyphKind::Sigil, {-60, 0}, 1, 0},
                  {"levitation", GlyphKind::Sign, {0, -120}, 1, 0}}});

  // Guidance: a human sigil or a second element sigil is the target
  {
    struct G {
      const char *name;
      std::vector<std::pair<const char *, float>> sigils;
    };
    const G guided[] = {
        {"guided-light-human", {{"light", 1.0f}, {"human", 0.8f}}},
        {"guided-fire-water", {{"fire", 1.2f}, {"water", 0.6f}}},
        {"guided-water-fire", {{"fire", 0.6f}, {"water", 1.2f}}},
        {"guided-tie", {{"earth", 1.0f}, {"water", 1.0f}}},
        {"guided-nothing", {{"fire", 1.0f}}},
        {"guided-air", {{"fire", 1.0f}, {"wind", 0.5f}}},
        {"guided-three", {{"fire", 1.0f}, {"water", 0.5f}, {"earth", 0.4f}}},
        {"guided-wind-human", {{"wind", 1.0f}, {"human", 1.0f}}},
        {"guided-flight-human", {{"wind_underfoot", 1.0f}, {"human", 1.0f}}},
        {"human-alone", {{"fire", 1.0f}, {"human", 1.0f}}},
    };
    for (const G &g : guided) {
      for (float guidance : {0.5f, 1.5f}) {
        bool withGuidance = std::string(g.name) != "human-alone";
        if (!withGuidance && guidance > 1.0f)
          continue;
        Spell s;
        s.name = std::string(g.name) + "-" + std::to_string(guidance);
        float x = -120;
        for (auto [id, scale] : g.sigils) {
          s.glyphs.push_back({id, GlyphKind::Sigil, {x, 40}, scale, 0.0f});
          x += 80;
        }
        if (withGuidance)
          s.glyphs.push_back(
              {"guidance", GlyphKind::Sigil, {0, 130}, guidance, 0.0f});
        s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 1.2f, 0.0f});
        if (std::string(g.name) == "guided-wind-human")
          s.glyphs.push_back({"pulling", GlyphKind::Sign, {90, 0}, 1.0f, 0.0f});
        out.push_back(std::move(s));
      }
    }
  }

  // Layered spells: 1-6 embedded spells (6 is too many), varied scales and
  // turns, with and without outer ring signs
  const char *partSigils[] = {"water", "fire", "earth", "wind", "wind_underfoot", "ice"};
  for (int parts = 1; parts <= 6; ++parts) {
    for (int ring = 0; ring < 3; ++ring) {
      Spell s;
      s.name = "layered" + std::to_string(parts) + "-" + std::to_string(ring);
      for (int i = 0; i < parts; ++i) {
        SpellComponent c;
        c.source = partSigils[i];
        c.scale = 0.2f + 0.06f * static_cast<float>((i * 3 + ring) % 6);
        c.rotationDeg = rotations[(i * 2 + ring) % 9] / 4.0f;
        c.position = {60.0f * i - 120.0f, 0.0f};
        c.glyphs.push_back(
            {partSigils[i], GlyphKind::Sigil, {0, 0}, 0.4f + 0.3f * i, 0.0f});
        c.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120},
                            0.5f + 0.2f * (i + ring), rotations[(i + ring) % 9]});
        if (i % 2 == 1)
          c.glyphs.push_back({"crushing", GlyphKind::Sign, {80, 0}, 0.8f, 0.0f,
                              ring == 1});
        s.components.push_back(std::move(c));
      }
      if (ring >= 1) {
        s.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -210}, 0.8f,
                            ring == 1 ? 0.0f : 45.0f});
        s.glyphs.push_back({"cooling", GlyphKind::Sign, {150, 150}, 0.6f, 0.0f});
      }
      if (ring == 2) {
        s.glyphs.push_back({"expansion", GlyphKind::Sign, {-150, 150}, 0.7f,
                            0.0f, true});
        s.glyphs.push_back({"collection", GlyphKind::Sign, {0, 210}, 0.5f,
                            180.0f});
      }
      out.push_back(std::move(s));
    }
  }
  {
    // A sigil in the outer ring is not allowed
    Spell s;
    s.name = "layered-ring-sigil";
    SpellComponent c;
    c.glyphs.push_back({"water", GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f});
    c.glyphs.push_back({"levitation", GlyphKind::Sign, {0, -120}, 1.0f, 0.0f});
    s.components.push_back(c);
    s.glyphs.push_back({"fire", GlyphKind::Sigil, {0, -210}, 0.5f, 0.0f});
    out.push_back(std::move(s));
  }
  return out;
}

} // namespace

TEST_CASE("write golden spell vectors", "[.generate]") {
  nlohmann::json cases = nlohmann::json::array();
  for (const Spell &s : Cases()) {
    nlohmann::json c{{"name", s.name},
                     {"stats", SpellQuant::Quantize(SpellSystem::Evaluate(s))}};
    SpellJson::Write(c, s);
    cases.push_back(std::move(c));
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

TEST_CASE("old spell files rename the wind sigils and the thrust sign",
          "[spell]") {
  Spell s;
  s.glyphs = {{"wind", GlyphKind::Sigil, {0, 0}, 1, 0},
              {"column", GlyphKind::Sign, {0, -120}, 1, 0}};
  SpellComponent c;
  c.glyphs = {{"gust", GlyphKind::Sigil, {0, 0}, 1, 0},
              {"column", GlyphKind::Sign, {0, -120}, 1, 0}};
  s.components.push_back(c);
  Spell v2 = s;
  SpellJson::MigrateLegacyIds(s, 1);
  CHECK(s.glyphs[0].assetId == "wind_underfoot");
  CHECK(s.glyphs[1].assetId == "levitation");
  CHECK(s.components[0].glyphs[0].assetId == "wind");
  CHECK(s.components[0].glyphs[1].assetId == "levitation");
  // Format 2 already has the new wind names: only the thrust sign moves
  v2.glyphs[0].assetId = "wind_underfoot";
  SpellJson::MigrateLegacyIds(v2, 2);
  CHECK(v2.glyphs[0].assetId == "wind_underfoot");
  CHECK(v2.glyphs[1].assetId == "levitation");
}

TEST_CASE("the editor's problem text agrees with the evaluator", "[spell]") {
  for (const Spell &s : Cases()) {
    INFO(s.name);
    bool valid = SpellSystem::Evaluate(s).valid;
    std::string problem = SpellSystem::Problem(s);
    // Never nothing wrong with an invalid spell, never a complaint about
    // the sigils of a valid one (a missing sign is the editor's own rule)
    if (problem.empty())
      CHECK(valid);
    if (valid && !problem.empty())
      CHECK(problem == "Add at least one sign.");
  }
}
