#include "whas/audio/synth.h"

// Flying on wind underfoot: air rushing past at the flyer's speed. Noise
// through a band-pass that opens up the faster they go, buffeted by a
// fluttering tremolo, over a low pressure rumble, with a thin whistle at
// speed. Taking off (Launch) adds a whoosh that sweeps up.
namespace {

class FlightSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    // Every take-off restarts the sweep
    m_whooshEnv.Start(0.04f, 0.6f);
    m_whooshGain = e.gain;
    m_whooshCutoff = 300.0f * e.pitch;
    m_whooshPan = e.pan;
    m_whoosh.Reset();
  }

  void SetState(const ProfileState &s) override {
    m_speed.SetTarget(s.level);
    m_pan.SetTarget(s.pan);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      float speed = m_speed.Next();
      if (--m_flutterCountdown <= 0) {
        m_flutterHz.SetTarget(m_rng.Range(8.0f, 15.0f));
        m_flutterCountdown =
            static_cast<int>(m_rng.Range(0.2f, 0.6f) * dsp::kSampleRate);
      }
      float flutterHz = m_flutterHz.Next();

      if ((i & 15) == 0) {
        m_rush.Set(350.0f + 2800.0f * speed, 0.6f);
        m_whistle.Set(1100.0f + 1400.0f * speed, 12.0f);
        dsp::PanGains(m_pan.Value(), m_panL, m_panR);
        // The take-off whoosh sweeps up through the band
        m_whooshCutoff = std::min(4500.0f, m_whooshCutoff * 1.006f);
        m_whoosh.Set(m_whooshCutoff, 1.4f);
      }
      m_pan.Next();

      float white = m_rng.Bipolar();
      float flutter = 1.0f + 0.4f * speed * m_flutter.Sine(flutterHz);
      float rush = m_rush.Process(white).band * 1.6f;
      float whistle = m_whistle.Process(white).band * speed * 0.6f;
      float rumble = m_rumbleLp.Process(m_brown.Next(m_rng)) * 0.9f;
      float s = (rush + whistle + rumble) * speed * flutter * 0.45f;

      float whoosh = m_whoosh.Process(white).band *
                     m_whooshEnv.Next() * m_whooshGain * 1.4f;
      float wl, wr;
      dsp::PanGains(m_whooshPan, wl, wr);
      l[i] += s * m_panL + whoosh * wl;
      r[i] += s * m_panR + whoosh * wr;
    }
    m_voicesInUse.store((m_speed.Value() > 0.01f) + !m_whooshEnv.Done(),
                        std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0xF1167u};
  dsp::BrownNoise m_brown;
  dsp::Svf m_rush, m_whistle, m_whoosh;
  dsp::OnePoleLP m_rumbleLp = [] {
    dsp::OnePoleLP lp;
    lp.SetCutoff(220.0f);
    return lp;
  }();
  dsp::Oscillator m_flutter;
  dsp::Smoothed m_speed{0.12f};
  dsp::Smoothed m_pan{0.1f};
  dsp::Smoothed m_flutterHz{0.3f, 11.0f};
  dsp::Envelope m_whooshEnv;
  float m_whooshGain = 0.0f;
  float m_whooshCutoff = 300.0f;
  float m_whooshPan = 0.0f;
  float m_panL = 0.707f, m_panR = 0.707f;
  int m_flutterCountdown = 0;
};

} // namespace

std::unique_ptr<Synth> MakeFlightSynth() { return std::make_unique<FlightSynth>(); }
