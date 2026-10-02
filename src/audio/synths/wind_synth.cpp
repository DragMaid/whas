#include "whas/audio/synth.h"

// Wind: pink and brown noise through a low-pass whose cutoff follows the
// force, wandering in slow gusts, with a thin whistle at full strength.
// An event (a gust spell starting) swells it for a moment.
namespace {

class WindSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    m_swell.Start(0.08f, 0.6f);
    m_swellGain = std::max(m_swellGain * m_swell.Value(), e.gain);
  }

  void SetState(const ProfileState &s) override {
    m_level.SetTarget(s.level);
    m_density.SetTarget(s.density);
    m_pan.SetTarget(s.pan);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      // A new gust target every ~0.3-1.5 s
      if (--m_gustCountdown <= 0) {
        m_gust.SetTarget(m_rng.Range(0.3f, 1.0f));
        m_gustCountdown = static_cast<int>(m_rng.Range(0.3f, 1.5f) *
                                           dsp::kSampleRate);
      }
      float gust = m_gust.Next();
      float swell = m_swell.Next() * m_swellGain;
      float force = std::min(1.0f, m_level.Next() + swell);
      float density = m_density.Next();

      // Cutoff only needs updating every few samples
      if ((i & 15) == 0) {
        m_lp.Set(200.0f + 2800.0f * force * (0.6f + 0.4f * gust), 0.8f);
        m_whistle.Set(600.0f + 900.0f * gust + 400.0f * density, 9.0f);
      }
      float noise = 0.6f * m_pink.Next(m_rng) + 0.4f * m_brown.Next(m_rng);
      float body = m_lp.Process(noise).low;
      float whistle = m_whistle.Process(noise).band * force * force * 0.25f;
      float s = (body + whistle) * force * (0.5f + 0.5f * gust) * 0.7f;

      if ((i & 63) == 0)
        dsp::PanGains(m_pan.Value(), m_panL, m_panR);
      m_pan.Next();
      l[i] += s * m_panL;
      r[i] += s * m_panR;
    }
    m_voicesInUse.store(m_level.Value() > 0.01f ? 1 : 0,
                        std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0x77122AB1u};
  dsp::PinkNoise m_pink;
  dsp::BrownNoise m_brown;
  dsp::Svf m_lp, m_whistle;
  dsp::Smoothed m_level{0.25f};
  dsp::Smoothed m_density{0.5f};
  dsp::Smoothed m_pan{0.3f};
  dsp::Smoothed m_gust{0.6f, 0.6f};
  dsp::Envelope m_swell;
  float m_swellGain = 0.0f;
  float m_panL = 0.707f, m_panR = 0.707f;
  int m_gustCountdown = 0;
};

} // namespace

std::unique_ptr<Synth> MakeWindSynth() { return std::make_unique<WindSynth>(); }
