#include "whas/audio/synth.h"

// Solid material shattering: a dull thud under a quick cluster of crunchy
// noise grains (a dozen tiny fractures over ~100 ms). The material sets the
// colour: earth is soft and low, rock hard and bright, ice glassy with
// resonant tinkling pings.
namespace {

struct Material {
  float grainLo, grainHi; // grain filter centre range, Hz
  float q;
  int grainsLo, grainsHi;
  float spread;   // seconds the grains are spread over
  float thud;     // gain of the low thud
  float pingGain; // ice's tonal tinkles
};

Material MaterialOf(Element element) {
  switch (element) {
  case Element::ICE:
    return {2500.0f, 7000.0f, 12.0f, 8, 14, 0.14f, 0.3f, 0.5f};
  case Element::ROCK:
    return {900.0f, 3500.0f, 1.8f, 8, 14, 0.10f, 1.0f, 0.0f};
  default: // earth, sand, wood...
    return {400.0f, 1600.0f, 1.2f, 6, 10, 0.12f, 0.9f, 0.0f};
  }
}

struct Shatter {
  bool active = false;
  Material material{};
  // Grains
  dsp::Envelope grainEnv;
  dsp::Svf grainFilter;
  dsp::Oscillator ping;
  float pingHz = 0.0f;
  float grainGain = 0.0f;
  int grainsLeft = 0;
  int nextGrain = 0; // samples until the next fracture
  // Thud
  dsp::Envelope thudEnv;
  dsp::OnePoleLP thudLp;
  float gain = 0.0f;
  float pitch = 1.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const {
    return std::max(grainEnv.Value(), thudEnv.Value()) * gain;
  }
  bool Done() const {
    return grainsLeft == 0 && grainEnv.Done() && thudEnv.Done();
  }
};

class BreakSynth final : public Synth {
public:
  void Trigger(const AudioEvent &e) override {
    Shatter &s = m_shatters.Allocate();
    s.active = true;
    s.material = MaterialOf(e.element);
    s.pitch = e.pitch;
    s.grainsLeft = s.material.grainsLo +
                   static_cast<int>(m_rng.Next() %
                                    (s.material.grainsHi - s.material.grainsLo + 1));
    s.nextGrain = 0;
    s.grainEnv.Kill();
    s.thudEnv.Start(0.001f, 0.08f);
    s.thudLp.SetCutoff(180.0f * e.pitch);
    s.gain = e.gain;
    dsp::PanGains(e.pan, s.left, s.right);
  }

  void Render(float *l, float *r, int frames) override {
    for (int i = 0; i < frames; ++i) {
      float outL = 0.0f, outR = 0.0f;
      m_shatters.ForEachActive([&](Shatter &s) {
        const Material &m = s.material;
        if (s.grainsLeft > 0 && --s.nextGrain <= 0) {
          // The next fracture: fresh pitch, quieter as the break dies away
          --s.grainsLeft;
          s.grainFilter.Reset();
          s.grainFilter.Set(m_rng.Range(m.grainLo, m.grainHi) * s.pitch, m.q);
          s.grainEnv.Start(0.0f, m_rng.Range(0.004f, 0.018f));
          s.grainGain = m_rng.Range(0.5f, 1.0f) *
                        (0.4f + 0.6f * s.grainsLeft / float(m.grainsHi));
          s.pingHz = m_rng.Range(2500.0f, 6000.0f) * s.pitch;
          s.nextGrain = static_cast<int>(m.spread / m.grainsHi *
                                         m_rng.Range(0.3f, 1.7f) *
                                         dsp::kSampleRate);
        }
        float noise = m_rng.Bipolar();
        float grainEnv = s.grainEnv.Next();
        float grain = s.grainFilter.Process(noise).band * grainEnv * s.grainGain *
                      (m.q > 4.0f ? 2.5f : 1.6f);
        if (m.pingGain > 0.0f)
          grain += s.ping.Sine(s.pingHz) * grainEnv * s.grainGain * m.pingGain;
        float thud = s.thudLp.Process(noise) * s.thudEnv.Next() * m.thud * 4.0f;
        float out = (grain + thud) * s.gain;
        outL += out * s.left;
        outR += out * s.right;
        if (s.Done())
          s.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_shatters.CountActive(), std::memory_order_relaxed);
  }

private:
  dsp::Rng m_rng{0xB2EA4u};
  VoicePool<Shatter, 8> m_shatters;
};

} // namespace

std::unique_ptr<Synth> MakeBreakSynth() { return std::make_unique<BreakSynth>(); }
