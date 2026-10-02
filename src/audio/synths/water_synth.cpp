#include "whas/audio/synth.h"

// Water: flowing water is band-passed noise whose resonance wanders, and
// settling drops are short sine bloops chirping upward
namespace {

struct Bloop {
  bool active = false;
  dsp::Oscillator osc;
  dsp::Envelope env;
  float freq = 500.0f;
  float chirp = 1.0f;    // pitch multiplier, rises
  float chirpMul = 1.0f; // per sample
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class WaterSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    // A spell's water striking splashes: a few drops at once
    int drops = e.kind == SoundKind::Impact ? 3 : 1;
    for (int i = 0; i < drops; ++i)
      Drop(e.gain, e.pan + 0.2f * m_rng.Bipolar(),
           e.pitch * m_rng.Range(0.8f, 1.2f));
  }

  void SetState(const ProfileState &s) override {
    m_level.SetTarget(s.level);
    m_density = s.density;
    m_pan = s.pan;
  }

  void Render(float *l, float *r, int frames) override {
    float bedL, bedR;
    dsp::PanGains(m_pan * 0.7f, bedL, bedR);
    for (int i = 0; i < frames; ++i) {
      float level = m_level.Next();
      if (--m_wanderCountdown <= 0) {
        m_resonance.SetTarget(m_rng.Range(400.0f, 1500.0f) *
                              (1.0f + 0.3f * m_density));
        m_wanderCountdown = static_cast<int>(m_rng.Range(0.02f, 0.06f) *
                                             dsp::kSampleRate);
      }
      float res = m_resonance.Next();
      if ((i & 7) == 0)
        m_flow.Set(res, 4.0f);
      // Flowing water also throws the odd droplet
      if (m_rng.Uniform() < level * 4.0f / dsp::kSampleRate)
        Drop(0.25f * level, m_pan + 0.4f * m_rng.Bipolar(), 1.0f);

      float flow = m_flow.Process(m_rng.Bipolar()).band * level * 0.07f;
      float outL = flow * bedL, outR = flow * bedR;
      m_bloops.ForEachActive([&](Bloop &b) {
        b.chirp *= b.chirpMul;
        float s = b.osc.Sine(b.freq * b.chirp) * b.env.Next() * b.gain;
        outL += s * b.left;
        outR += s * b.right;
        if (b.env.Done())
          b.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_bloops.CountActive(), std::memory_order_relaxed);
  }

private:
  void Drop(float gain, float pan, float pitch) {
    Bloop &b = m_bloops.Allocate();
    b.active = true;
    b.osc.Reset();
    b.freq = m_rng.Range(300.0f, 900.0f) * pitch;
    float length = m_rng.Range(0.03f, 0.08f);
    b.chirp = 1.0f;
    b.chirpMul = std::exp(std::log(1.8f) / (length * dsp::kSampleRate));
    b.env.Start(0.001f, length);
    b.gain = gain * 0.12f;
    dsp::PanGains(pan, b.left, b.right);
  }

  dsp::Rng m_rng{0x0A7E2u};
  dsp::Svf m_flow;
  dsp::Smoothed m_level{0.2f};
  dsp::Smoothed m_resonance{0.03f, 800.0f};
  VoicePool<Bloop, 12> m_bloops;
  float m_density = 0.0f;
  float m_pan = 0.0f;
  int m_wanderCountdown = 0;
};

} // namespace

std::unique_ptr<Synth> MakeWaterSynth() { return std::make_unique<WaterSynth>(); }
