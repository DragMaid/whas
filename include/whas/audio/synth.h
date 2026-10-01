#pragma once
#include "whas/audio/audio_types.h"
#include "whas/audio/dsp.h"
#include <array>
#include <atomic>
#include <memory>

// One procedural sound profile. Trigger, SetState and Render run on the
// audio thread (the manager hands events over through a queue), so they
// must not allocate or block.
class Synth {
public:
  virtual ~Synth() = default;
  // A one-shot impact
  virtual void Trigger(const AudioEvent &event) { (void)event; }
  // The continuous bed for this audio block
  virtual void SetState(const ProfileState &state) { (void)state; }
  // Mix `frames` samples into l and r (adds, doesn't clear)
  virtual void Render(float *l, float *r, int frames) = 0;

  // Voices sounding after the last block; safe to read from any thread
  int VoicesInUse() const { return m_voicesInUse.load(std::memory_order_relaxed); }

protected:
  std::atomic<int> m_voicesInUse{0};
};

// Fixed polyphony. A Voice has `bool active` and `float Loudness() const`;
// when every voice is busy, the quietest one is stolen.
template <typename Voice, size_t N> class VoicePool {
public:
  Voice &Allocate() {
    Voice *quietest = &m_voices[0];
    for (Voice &v : m_voices) {
      if (!v.active)
        return v;
      if (v.Loudness() < quietest->Loudness())
        quietest = &v;
    }
    return *quietest;
  }
  template <typename Fn> void ForEachActive(Fn &&fn) {
    for (Voice &v : m_voices)
      if (v.active)
        fn(v);
  }
  int CountActive() const {
    int n = 0;
    for (const Voice &v : m_voices)
      n += v.active;
    return n;
  }

private:
  std::array<Voice, N> m_voices{};
};

// One per profile (src/audio/synths/)
std::unique_ptr<Synth> MakeSandSynth();
std::unique_ptr<Synth> MakeWindSynth();
std::unique_ptr<Synth> MakeFireSynth();
std::unique_ptr<Synth> MakeEarthSynth();
std::unique_ptr<Synth> MakeWaterSynth();
std::unique_ptr<Synth> MakeLightSynth();
std::unique_ptr<Synth> MakeSpellSynth();
std::unique_ptr<Synth> MakeBreakSynth();
std::unique_ptr<Synth> MakeFlightSynth();
