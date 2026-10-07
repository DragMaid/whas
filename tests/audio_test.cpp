#include "whas/audio/audio_manager.h"
#include "whas/audio/audio_observer.h"
#include "whas/audio/element_sound_map.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float DT = 1.0f / 60.0f;

void BuildScene(Simulation &sim) {
  for (int x = 0; x < GRID_W; x += 3)
    sim.Paint(x, GRID_H - 4, Element::ROCK, 3);
  sim.Paint(60, 60, Element::EARTH, 10);
  sim.Paint(120, 40, Element::SAND, 12);
  sim.Paint(200, 50, Element::WATER, 14);
  sim.Paint(160, 140, Element::FIRE, 5);
  sim.Paint(280, 40, Element::STEAM, 6);
  for (int y = GRID_H - 30; y < GRID_H - 8; ++y)
    sim.Paint(170, y, Element::WOOD, 1);
}

} // namespace

TEST_CASE("every sounding element maps to a real profile", "[audio]") {
  for (size_t e = 0; e < static_cast<size_t>(Element::COUNT); ++e) {
    const ElementSound &sound = SoundOf(static_cast<Element>(e));
    if (sound.profile == SoundProfile::None)
      continue;
    CHECK(sound.profile < SoundProfile::COUNT);
    CHECK(sound.weight > 0.0f);
    CHECK((sound.onMove || sound.onSettle));
  }
  CHECK(SoundOf(Element::SAND).profile == SoundProfile::Sand);
  CHECK(SoundOf(Element::EARTH).profile == SoundProfile::Earth);
  CHECK(SoundOf(Element::ROCK).profile == SoundProfile::None); // bodies thump
  CHECK(SoundOf(Element::WATER).profile == SoundProfile::Water);
  CHECK(SoundOf(Element::AIR).profile == SoundProfile::None);
}

TEST_CASE("casts and their impacts are logged for sound, not hashed",
          "[audio]") {
  Spell spell;
  spell.name = "fire";
  spell.glyphs.push_back({"fire", GlyphKind::Sigil, {0, 0}, 1.0f, 0.0f});

  Simulation logged, plain;
  for (Simulation *sim : {&logged, &plain}) {
    sim->Restart(3);
    for (int x = 0; x < GRID_W; x += 3)
      sim->Paint(x, GRID_H - 4, Element::ROCK, 3);
    sim->CastSpell(spell, {40.0f, GRID_H - 30.0f}, {0.3f, 1.0f});
  }
  bool cast = false, impact = false;
  for (int i = 0; i < 240; ++i) {
    logged.Update(DT);
    plain.Update(DT);
    for (const ParticleNoise &n : logged.GetParticleSystem().TakeNoises()) {
      cast |= n.kind == ParticleNoise::Cast && n.element == Element::FIRE;
      impact |= n.kind == ParticleNoise::Impact && n.element == Element::FIRE;
    }
  }
  CHECK(cast);
  CHECK(impact);
  CHECK(logged.StateHash() == plain.StateHash());
}

TEST_CASE("a falling rock is heard as one body, not its cells", "[audio]") {
  Simulation sim;
  sim.Restart(5);
  for (int x = 0; x < GRID_W; ++x)
    for (int y = GRID_H - 6; y < GRID_H; ++y)
      sim.Paint(x, y, Element::EARTH, 0);
  for (int x = 100; x < 112; ++x)
    for (int y = 60; y < 70; ++y)
      sim.Paint(x, y, Element::ROCK, 0);

  int hits = 0, rockNoises = 0;
  float strongest = 0.0f;
  for (int i = 0; i < 600; ++i) {
    sim.Update(DT);
    for (const ParticleNoise &n : sim.GetParticleSystem().TakeNoises()) {
      if (n.kind == ParticleNoise::BodyHit) {
        ++hits;
        strongest = std::max(strongest, n.strength);
        CHECK(n.element == Element::ROCK);
        CHECK(n.heft > 0.0f);
      } else if (n.element == Element::ROCK) {
        ++rockNoises;
      }
    }
  }
  CHECK(hits >= 1);
  CHECK(hits < 10); // a landing and a bounce or two, not a rattle
  CHECK(strongest > 0.5f);
  CHECK(rockNoises == 0);
}

TEST_CASE("the bus never clips under a flood of events", "[audio]") {
  AudioManager audio; // no device: mixed by hand
  audio.Settings().master = 1.0f;
  audio.Settings().ambient = 1.0f;
  audio.Settings().profile.fill(2.0f); // the loudest the sliders allow
  audio.Update();
  for (size_t p = 0; p < SOUND_PROFILE_COUNT; ++p)
    audio.SetProfileState(static_cast<SoundProfile>(p), {1.0f, 1.0f, 0.0f});

  constexpr unsigned kChunk = 512;
  std::vector<float> buffer(kChunk * 2);
  float peak = 0.0f, energy = 0.0f;
  int posted = 0;
  // One second, with 10,000 impacts spread over it
  for (unsigned done = 0; done < AudioManager::kSampleRate; done += kChunk) {
    for (int i = 0; i < 110; ++i, ++posted)
      audio.Post({static_cast<SoundProfile>(posted % SOUND_PROFILE_COUNT), 1.0f,
                  (posted % 3) - 1.0f, 0.5f + (posted % 7) * 0.2f,
                  static_cast<SoundKind>(posted % 6),
                  static_cast<Element>(posted % static_cast<int>(Element::COUNT))});
    audio.Mix(buffer.data(), kChunk);
    for (float s : buffer) {
      REQUIRE(std::isfinite(s));
      peak = std::max(peak, std::fabs(s));
      energy += s * s;
    }
  }
  CHECK(posted >= 10000);
  CHECK(peak <= 1.0f);
  CHECK(energy > 0.0f);
  CHECK(audio.LimiterGain() < 1.0f); // it really was pushed

  // Muted is silent once the fade has run
  audio.Settings().muted = true;
  audio.Update();
  audio.Mix(buffer.data(), kChunk);
  audio.Mix(buffer.data(), kChunk);
  for (float s : buffer)
    CHECK(s == 0.0f);
}

TEST_CASE("listening doesn't change the simulation", "[audio]") {
  Simulation quiet, heard;
  quiet.Restart(7);
  heard.Restart(7);
  BuildScene(quiet);
  BuildScene(heard);

  AudioManager audio;
  AudioObserver observer;
  float loudestSand = 0.0f, loudestFire = 0.0f;
  for (int i = 0; i < 300; ++i) {
    quiet.Update(DT);
    heard.Update(DT);
    observer.Update(heard, audio, DT);
    loudestSand = std::max(loudestSand, audio.Level(SoundProfile::Sand));
    loudestFire = std::max(loudestFire, audio.Level(SoundProfile::Fire));
  }
  CHECK(quiet.StateHash() == heard.StateHash());
  CHECK(loudestSand > 0.0f); // the sand fell and was heard
  CHECK(loudestFire > 0.0f);
}
