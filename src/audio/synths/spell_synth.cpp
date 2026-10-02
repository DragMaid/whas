#include "whas/audio/synth.h"

// A spell leaving the caster: a noise whoosh through a resonant filter that
// sweeps, under a tone that glides, both shaped per element by a preset.
// A new element's cast sound is one row in CastPresetOf.
namespace {

struct CastPreset {
  float noiseFrom, noiseTo; // filter sweep, Hz
  float q;
  float noiseGain;
  float toneFrom, toneTo; // tone glide, Hz (0 = no tone)
  float toneGain;
  float attack, decay; // seconds
};

CastPreset CastPresetOf(Element element) {
  switch (element) {
  case Element::FIRE: // roaring rush up, warm low tone
    return {300.0f, 3500.0f, 0.9f, 1.0f, 90.0f, 140.0f, 0.35f, 0.02f, 0.35f};
  case Element::WATER: // bubbly downward sweep with a liquid chirp
    return {2500.0f, 500.0f, 3.0f, 0.6f, 350.0f, 800.0f, 0.25f, 0.01f, 0.3f};
  case Element::EARTH:
  case Element::SAND: // gritty low heave
    return {200.0f, 900.0f, 1.2f, 1.0f, 70.0f, 45.0f, 0.5f, 0.015f, 0.3f};
  case Element::ROCK: // heavier, deeper
    return {150.0f, 700.0f, 1.2f, 1.0f, 55.0f, 35.0f, 0.6f, 0.01f, 0.35f};
  case Element::ICE: // glassy, high and resonant
    return {2000.0f, 7000.0f, 8.0f, 0.5f, 1400.0f, 2200.0f, 0.15f, 0.005f, 0.4f};
  case Element::LIGHT: // a rising shimmer
    return {3000.0f, 9000.0f, 5.0f, 0.25f, 600.0f, 1800.0f, 0.3f, 0.01f, 0.45f};
  default: // a plain whoosh
    return {400.0f, 2500.0f, 1.5f, 0.8f, 0.0f, 0.0f, 0.0f, 0.03f, 0.3f};
  }
}

struct Cast {
  bool active = false;
  dsp::Envelope env;
  dsp::Svf filter;
  dsp::Oscillator osc;
  float cutoff = 1000.0f, cutoffMul = 1.0f;
  float tone = 0.0f, toneMul = 1.0f;
  float q = 1.0f;
  float noiseGain = 1.0f, toneGain = 0.0f;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class SpellSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    CastPreset p = CastPresetOf(e.element);
    Cast &c = m_casts.Allocate();
    c.active = true;
    float jitter = m_rng.Range(0.92f, 1.08f) * e.pitch;
    float length = p.attack + p.decay;
    c.cutoff = p.noiseFrom * jitter;
    c.cutoffMul = std::exp(std::log(p.noiseTo / p.noiseFrom) /
                           (length * dsp::kSampleRate / 8.0f));
    c.tone = p.toneFrom * jitter;
    c.toneMul = p.toneFrom > 0.0f
                    ? std::exp(std::log(p.toneTo / p.toneFrom) /
                               (length * dsp::kSampleRate))
                    : 1.0f;
    c.q = p.q;
    c.noiseGain = p.noiseGain;
    c.toneGain = p.toneGain;
    c.filter.Reset();
    c.filter.Set(c.cutoff, c.q);
    c.osc.Reset();
    c.env.Start(p.attack, p.decay);
    c.gain = e.gain * 0.6f;
    dsp::PanGains(e.pan, c.left, c.right);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      float outL = 0.0f, outR = 0.0f;
      bool retune = (i & 7) == 0;
      m_casts.ForEachActive([&](Cast &c) {
        if (retune) {
          c.cutoff *= c.cutoffMul;
          c.filter.Set(c.cutoff, c.q);
        }
        c.tone *= c.toneMul;
        float noise = c.filter.Process(m_rng.Bipolar()).band * c.noiseGain;
        float tone = c.toneGain > 0.0f ? c.osc.Sine(c.tone) * c.toneGain : 0.0f;
        float s = (noise + tone) * c.env.Next() * c.gain;
        outL += s * c.left;
        outR += s * c.right;
        if (c.env.Done())
          c.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_casts.CountActive(), std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0x5BE11u};
  VoicePool<Cast, 8> m_casts;
};

} // namespace

std::unique_ptr<Synth> MakeSpellSynth() { return std::make_unique<SpellSynth>(); }
