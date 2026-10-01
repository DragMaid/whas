#include "whas/audio/synth.h"

// Light bursting: a shimmering chime of detuned, inharmonic sine partials
// with a bell envelope (upper partials fade first) and a little vibrato
namespace {

constexpr int kPartials = 4;
constexpr float kRatios[kPartials] = {1.0f, 2.01f, 2.76f, 5.4f};
constexpr float kAmps[kPartials] = {0.5f, 0.25f, 0.18f, 0.08f};

struct Chime {
  bool active = false;
  std::array<dsp::Oscillator, kPartials> partials;
  dsp::Oscillator vibrato;
  dsp::Envelope env;
  float freq = 900.0f;
  float detune = 1.0f;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class LightSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    Chime &c = m_chimes.Allocate();
    c.active = true;
    for (dsp::Oscillator &o : c.partials)
      o.Reset(m_rng.Uniform());
    c.freq = m_rng.Range(700.0f, 1200.0f) * e.pitch;
    c.detune = 1.0f + m_rng.Range(0.002f, 0.006f);
    c.env.Start(0.003f, m_rng.Range(0.6f, 1.2f));
    c.gain = e.gain * 0.5f;
    dsp::PanGains(e.pan, c.left, c.right);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      float outL = 0.0f, outR = 0.0f;
      m_chimes.ForEachActive([&](Chime &c) {
        float e = c.env.Next();
        float wobble = 1.0f + 0.003f * c.vibrato.Sine(5.5f);
        float s = 0.0f, fade = e;
        for (int p = 0; p < kPartials; ++p) {
          float hz = c.freq * kRatios[p] * wobble * (p & 1 ? c.detune : 1.0f);
          s += c.partials[p].Sine(hz) * kAmps[p] * fade;
          fade *= e; // higher partials decay faster
        }
        s *= c.gain;
        outL += s * c.left;
        outR += s * c.right;
        if (c.env.Done())
          c.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_chimes.CountActive(), std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0x11647u};
  VoicePool<Chime, 6> m_chimes;
};

} // namespace

std::unique_ptr<Synth> MakeLightSynth() { return std::make_unique<LightSynth>(); }
