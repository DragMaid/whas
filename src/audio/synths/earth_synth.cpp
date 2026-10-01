#include "whas/audio/synth.h"

// Earth and stone: a sub-bass thump (30-80 Hz sine/triangle sweeping down)
// driven into soft saturation so its harmonics carry on small speakers,
// a low-mid body thud and a gritty knock for the attack. Heavier settles
// (lower pitch from the observer) are deeper and longer; a spell's earth
// slamming in (Impact) is the biggest of all.
namespace {

struct Thump {
  bool active = false;
  dsp::Oscillator osc;
  dsp::Envelope env;
  dsp::Envelope bodyEnv;
  dsp::Envelope knockEnv;
  dsp::Svf body;
  dsp::Svf knock;
  float freq = 50.0f;
  float sweep = 1.0f;    // pitch multiplier, falls to 1
  float sweepMul = 1.0f; // per sample
  float drive = 1.0f;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class EarthSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    bool impact = e.kind == SoundKind::Impact;
    Thump &t = m_thumps.Allocate();
    t.active = true;
    t.osc.Reset();
    t.freq = std::clamp(m_rng.Range(40.0f, 70.0f) * e.pitch, 30.0f, 80.0f);
    float sweepFrom = impact ? 2.6f : 2.0f;
    t.sweep = sweepFrom;
    t.sweepMul =
        std::exp(std::log(1.0f / sweepFrom) / (0.05f * dsp::kSampleRate));
    float length = m_rng.Range(0.15f, 0.4f) / std::max(e.pitch, 0.5f);
    t.env.Start(0.001f, impact ? length * 1.5f : length);
    t.bodyEnv.Start(0.001f, m_rng.Range(0.06f, 0.12f));
    t.knockEnv.Start(0.0f, m_rng.Range(0.012f, 0.025f));
    t.body.Reset();
    t.body.Set(m_rng.Range(110.0f, 180.0f) * e.pitch, 1.6f);
    t.knock.Reset();
    t.knock.Set(m_rng.Range(500.0f, 1200.0f), 0.9f);
    t.drive = impact ? 3.0f : 2.2f;
    t.gain = std::min(1.0f, e.gain * (impact ? 1.3f : 1.0f));
    dsp::PanGains(e.pan * 0.6f, t.left, t.right);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      float outL = 0.0f, outR = 0.0f;
      m_thumps.ForEachActive([&](Thump &t) {
        if (t.sweep > 1.0f)
          t.sweep = std::max(1.0f, t.sweep * t.sweepMul);
        float hz = t.freq * t.sweep;
        float sub = 0.7f * t.osc.Sine(hz) + 0.3f * t.osc.Triangle(hz);
        // Saturation adds the upper harmonics a laptop speaker can play
        float thump = std::tanh(sub * t.env.Next() * t.drive) * 0.8f;
        float noise = m_rng.Bipolar();
        float body = t.body.Process(noise).band * t.bodyEnv.Next() * 2.2f;
        float knock = t.knock.Process(noise).band * t.knockEnv.Next() * 1.4f;
        float s = (thump + body + knock) * t.gain;
        outL += s * t.left;
        outR += s * t.right;
        if (t.env.Done())
          t.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_thumps.CountActive(), std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0xEA27B00Fu};
  VoicePool<Thump, 10> m_thumps;
};

} // namespace

std::unique_ptr<Synth> MakeEarthSynth() { return std::make_unique<EarthSynth>(); }
