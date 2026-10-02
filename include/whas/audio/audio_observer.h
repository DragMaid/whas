#pragma once
#include "whas/audio/audio_types.h"
#include "whas/audio/dsp.h"
#include <array>
#include <cstdint>
#include <vector>

class AudioManager;
class Simulation;

// Turns what happened in the world since the last tick into sound, on the
// main thread once per frame. It reads the simulation and only takes the
// particle system's noise log (sound-only data), so the world is unchanged.
//
// A cell whose element changed since the last tick is a move; one that
// changed the tick before but not this one has just settled. Moves feed a
// profile's continuous bed, settles become one-shot impacts. Hundreds of
// settles in a tick are batched into a handful of events whose gain grows
// with the square root of the count, so a landslide is louder than a grain
// without being hundreds of times louder.
class AudioObserver {
public:
  // How each profile turns counts into sound
  struct Tuning {
    int maxEventsPerTick = 0; // impacts per tick at most
    float settleFull = 1.0f;  // settle weight that gives a full-gain impact
    float bedScale = 1.0f;    // move weight that gives a ~63% bed level
    float densityScale = 1.0f;
    float minInterval = 0.0f; // seconds between impacts (0 = every tick)
  };

  AudioObserver();

  void Update(Simulation &sim, AudioManager &audio, float dt);
  // Forget the last frame (the next Update only takes a snapshot)
  void Reset() { m_primed = false; }

private:
  static constexpr int kSamples = 8; // impact positions kept per tick

  struct Tally {
    float moveWeight = 0.0f;
    float settleWeight = 0.0f;
    float sumX = 0.0f; // weighted, for pan
    float weight = 0.0f;
    int samples = 0;
    std::array<float, kSamples> sampleX{};
    int seen = 0; // settles offered to the reservoir
  };

  void Scan(const Simulation &sim);
  void ScanSpells(const Simulation &sim, AudioManager &audio);
  void Emit(AudioManager &audio, float dt);
  // Casts, spell impacts, breaks and fizzles from the particle system
  void HearNoises(Simulation &sim, AudioManager &audio);
  void Sample(Tally &t, float x);

  std::vector<uint8_t> m_prev;   // element per cell at the last tick
  std::vector<uint8_t> m_moving; // 1 if the cell changed at the last tick
  bool m_primed = false;
  uint32_t m_lastFrame = 0;
  uint32_t m_lastBurst = 0;
  int m_lastFieldCount = 0;
  bool m_ticked = false; // the world advanced since the last Update

  std::array<Tally, SOUND_PROFILE_COUNT> m_tally{};
  std::array<Tuning, SOUND_PROFILE_COUNT> m_tuning{};
  std::array<float, SOUND_PROFILE_COUNT> m_cooldown{};
  float m_windForce = 0.0f; // from field spells this tick
  dsp::Rng m_rng{0xA0D10u};
};
