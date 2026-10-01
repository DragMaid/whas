#include "whas/audio/synth.h"

// Fire: a warm low rumble that flickers with the size of the blaze, and
// sparse high-passed crackles whose rate grows with it. A fire spell
// striking (Impact) bursts in with a "whoomp" (noise through a low-pass that
// flares open and closes) and a shower of crackles; fire meeting water
// (Fizzle) hisses.
namespace {

struct Crackle {
  bool active = false;
  dsp::Envelope env;
  dsp::OnePoleHP hp;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

// Whoomps and hisses: filtered noise whose cutoff glides from one value to
// another over the envelope
struct Burst {
  bool active = false;
  bool hiss = false;
  dsp::Envelope env;
  dsp::Svf filter;
  float cutoff = 200.0f;
  float cutoffMul = 1.0f; // per sample
  float cutoffEnd = 200.0f;
  float gain = 0.0f;
  float left = 0.0f, right = 0.0f;
  float Loudness() const { return env.Value() * gain; }
};

class FireSynth final : public Synth {
public:
  FireSynth() {
    m_rumbleLp.SetCutoff(150.0f);
    m_rumbleLp2.SetCutoff(150.0f);
  }

  void Trigger(const AudioEvent &e) override {
    switch (e.kind) {
    case SoundKind::Impact:
      Whoomp(e);
      for (int i = 0; i < 5; ++i)
        Spark(e.gain * m_rng.Range(0.4f, 0.9f), e.pan + 0.4f * m_rng.Bipolar(),
              1.0f);
      break;
    case SoundKind::Fizzle:
      Hiss(e);
      break;
    default:
      Spark(e.gain, e.pan, e.pitch);
      break;
    }
  }

  void SetState(const ProfileState &s) override {
    m_level.SetTarget(s.level);
    m_density = s.density;
    m_pan = s.pan;
  }

  void Render(float *l, float *r, int frames) override {
    float bedL, bedR;
    dsp::PanGains(m_pan * 0.5f, bedL, bedR);
    for (int i = 0; i < frames; ++i) {
      float level = m_level.Next();
      if (--m_flickerCountdown <= 0) {
        m_flicker.SetTarget(m_rng.Range(0.5f, 1.0f));
        m_flickerCountdown = static_cast<int>(m_rng.Range(0.05f, 0.25f) *
                                              dsp::kSampleRate);
      }
      // Poisson crackles: a few a second for embers, dozens for a blaze
      float rate = level * (3.0f + 60.0f * m_density);
      if (m_rng.Uniform() < rate / dsp::kSampleRate)
        Spark(m_rng.Range(0.3f, 1.0f) * (0.4f + 0.6f * level),
              m_pan + 0.5f * m_rng.Bipolar(), 1.0f);

      float rumble =
          m_rumbleLp2.Process(m_rumbleLp.Process(m_brown.Next(m_rng))) *
          level * m_flicker.Next() * 1.4f;
      float outL = rumble * bedL, outR = rumble * bedR;
      m_crackles.ForEachActive([&](Crackle &c) {
        float s = c.hp.Process(m_rng.Bipolar()) * c.env.Next() * c.gain;
        outL += s * c.left;
        outR += s * c.right;
        if (c.env.Done())
          c.active = false;
      });
      bool retune = (i & 7) == 0;
      m_bursts.ForEachActive([&](Burst &b) {
        if (retune) {
          b.cutoff = b.cutoffMul > 1.0f ? std::min(b.cutoffEnd, b.cutoff * b.cutoffMul)
                                        : std::max(b.cutoffEnd, b.cutoff * b.cutoffMul);
          b.filter.Set(b.cutoff, b.hiss ? 0.8f : 1.2f);
        }
        float noise = b.hiss ? m_rng.Bipolar() : m_brown.Next(m_rng) * 0.6f +
                                                     m_rng.Bipolar() * 0.4f;
        auto f = b.filter.Process(noise);
        float s = (b.hiss ? f.high : f.low) * b.env.Next() * b.gain;
        outL += s * b.left;
        outR += s * b.right;
        if (b.env.Done())
          b.active = false;
      });
      l[i] += outL;
      r[i] += outR;
    }
    m_voicesInUse.store(m_crackles.CountActive() + m_bursts.CountActive(),
                        std::memory_order_relaxed);
  }

private:
  void Spark(float gain, float pan, float pitch) {
    Crackle &c = m_crackles.Allocate();
    c.active = true;
    c.hp.SetCutoff(m_rng.Range(2000.0f, 5000.0f) * pitch);
    c.env.Start(0.0f, m_rng.Range(0.001f, 0.004f));
    c.gain = gain * 0.6f;
    dsp::PanGains(pan, c.left, c.right);
  }

  // Cutoff glides from `from` to `to` (Hz) over `seconds`, retuned every 8
  // samples
  void Glide(Burst &b, float from, float to, float seconds) {
    b.cutoff = from;
    b.cutoffEnd = to;
    b.cutoffMul = std::exp(std::log(to / from) / (seconds * dsp::kSampleRate / 8.0f));
    b.filter.Reset();
    b.filter.Set(from, 1.0f);
  }

  void Whoomp(const AudioEvent &e) {
    Burst &b = m_bursts.Allocate();
    b.active = true;
    b.hiss = false;
    Glide(b, 250.0f * e.pitch, 2200.0f * e.pitch, 0.08f);
    b.env.Start(0.015f, m_rng.Range(0.25f, 0.4f));
    b.gain = e.gain * 2.4f;
    dsp::PanGains(e.pan, b.left, b.right);
  }

  void Hiss(const AudioEvent &e) {
    Burst &b = m_bursts.Allocate();
    b.active = true;
    b.hiss = true;
    Glide(b, 2500.0f, 6000.0f, 0.3f);
    b.env.Start(0.01f, m_rng.Range(0.25f, 0.45f));
    b.gain = e.gain * 0.35f;
    dsp::PanGains(e.pan, b.left, b.right);
  }

  dsp::Rng m_rng{0xF12E0001u};
  dsp::BrownNoise m_brown;
  dsp::OnePoleLP m_rumbleLp, m_rumbleLp2;
  dsp::Smoothed m_level{0.3f};
  dsp::Smoothed m_flicker{0.08f, 0.8f};
  VoicePool<Crackle, 24> m_crackles;
  VoicePool<Burst, 6> m_bursts;
  float m_density = 0.0f;
  float m_pan = 0.0f;
  int m_flickerCountdown = 0;
};

} // namespace

std::unique_ptr<Synth> MakeFireSynth() { return std::make_unique<FireSynth>(); }
