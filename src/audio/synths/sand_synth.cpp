#include "whas/audio/synth.h"

// Granular sand: very short band/high-passed noise bursts, every grain with
// its own pitch, length and pan, over a faint hiss while sand is flowing
namespace {

struct Grain {
  bool active = false;
  dsp::Envelope env;
  dsp::Svf filter;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class SandSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    Grain &g = m_grains.Allocate();
    g.active = true;
    float jitter = m_rng.Range(0.7f, 1.3f);
    float center = (3000.0f + 5000.0f * m_rng.Uniform()) * e.pitch * jitter *
                   (1.0f + 0.4f * m_density.Value());
    g.filter.Reset();
    g.filter.Set(center, 1.4f);
    g.env.Start(0.0003f, m_rng.Range(0.002f, 0.015f));
    g.gain = e.gain * m_rng.Range(0.5f, 1.0f);
    dsp::PanGains(e.pan + 0.2f * m_rng.Bipolar(), g.left, g.right);
  }

  void SetState(const ProfileState &s) override {
    m_bed.SetTarget(s.level);
    m_density.SetTarget(s.density);
    m_pan = s.pan;
  }

  void Render(float *l, float *r, int frames) override {
    float bedL, bedR;
    dsp::PanGains(m_pan, bedL, bedR);
    m_hiss.SetCutoff(4000.0f + 3000.0f * m_density.Value());
    for (int i = 0; i < frames; ++i) {
      float hiss = m_hiss.Process(m_rng.Bipolar()) * m_bed.Next() * 0.06f;
      m_density.Next();
      float outL = hiss * bedL, outR = hiss * bedR;
      m_grains.ForEachActive([&](Grain &g) {
        auto f = g.filter.Process(m_rng.Bipolar());
        float s = (0.7f * f.band + 0.3f * f.high) * g.env.Next() * g.gain;
        outL += s * g.left;
        outR += s * g.right;
        if (g.env.Done())
          g.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_grains.CountActive(), std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0x5A4D1234u};
  VoicePool<Grain, 24> m_grains;
  dsp::OnePoleHP m_hiss;
  dsp::Smoothed m_bed{0.08f};
  dsp::Smoothed m_density{0.2f};
  float m_pan = 0.0f;
};

} // namespace

std::unique_ptr<Synth> MakeSandSynth() { return std::make_unique<SandSynth>(); }
