#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Small real-time-safe building blocks for the synths: no allocation, no
// locks. None of this touches the simulation's random streams.
namespace dsp {

constexpr float kSampleRate = 48000.0f;
constexpr float kTwoPi = 6.28318530718f;

// xorshift32; every synth owns one so threads never share state
class Rng {
public:
  explicit Rng(uint32_t seed = 0x9E3779B9u) : m_state(seed ? seed : 1u) {}
  uint32_t Next() {
    m_state ^= m_state << 13;
    m_state ^= m_state >> 17;
    m_state ^= m_state << 5;
    return m_state;
  }
  float Uniform() { return (Next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
  float Bipolar() { return Uniform() * 2.0f - 1.0f; }               // [-1,1)
  float Range(float lo, float hi) { return lo + (hi - lo) * Uniform(); }

private:
  uint32_t m_state;
};

// Paul Kellet's economy pink filter over white noise
class PinkNoise {
public:
  float Next(Rng &rng) {
    float white = rng.Bipolar();
    m_b0 = 0.99765f * m_b0 + white * 0.0990460f;
    m_b1 = 0.96300f * m_b1 + white * 0.2965164f;
    m_b2 = 0.57000f * m_b2 + white * 1.0526913f;
    return (m_b0 + m_b1 + m_b2 + white * 0.1848f) * 0.2f;
  }

private:
  float m_b0 = 0.0f, m_b1 = 0.0f, m_b2 = 0.0f;
};

// Leaky integrated white noise
class BrownNoise {
public:
  float Next(Rng &rng) {
    m_last = (m_last + 0.02f * rng.Bipolar()) / 1.02f;
    return m_last * 3.5f;
  }

private:
  float m_last = 0.0f;
};

class OnePoleLP {
public:
  void SetCutoff(float hz) {
    m_a = 1.0f - std::exp(-kTwoPi * std::clamp(hz, 1.0f, 20000.0f) /
                          kSampleRate);
  }
  float Process(float x) { return m_z += m_a * (x - m_z); }

private:
  float m_a = 1.0f;
  float m_z = 0.0f;
};

class OnePoleHP {
public:
  void SetCutoff(float hz) { m_lp.SetCutoff(hz); }
  float Process(float x) { return x - m_lp.Process(x); }

private:
  OnePoleLP m_lp;
};

// Chamberlin-style state variable filter (Simper's trapezoidal form, stable
// at any cutoff). One call gives low, band and high pass at once.
class Svf {
public:
  struct Out {
    float low, band, high;
  };
  void Set(float hz, float q) {
    float g = std::tan(3.14159265f * std::clamp(hz, 10.0f, 20000.0f) /
                       kSampleRate);
    m_k = 1.0f / std::max(q, 0.1f);
    m_a1 = 1.0f / (1.0f + g * (g + m_k));
    m_a2 = g * m_a1;
    m_a3 = g * m_a2;
  }
  Out Process(float x) {
    float v3 = x - m_ic2;
    float v1 = m_a1 * m_ic1 + m_a2 * v3;
    float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
    m_ic1 = 2.0f * v1 - m_ic1;
    m_ic2 = 2.0f * v2 - m_ic2;
    return {v2, v1, x - m_k * v1 - v2};
  }
  void Reset() { m_ic1 = m_ic2 = 0.0f; }

private:
  float m_k = 1.0f, m_a1 = 0.0f, m_a2 = 0.0f, m_a3 = 0.0f;
  float m_ic1 = 0.0f, m_ic2 = 0.0f;
};

// Linear attack, exponential decay; Done() once it has faded out
class Envelope {
public:
  void Start(float attackSec, float decaySec) {
    m_attackStep = attackSec > 0.0f ? 1.0f / (attackSec * kSampleRate) : 1.0f;
    m_decayMul = std::exp(-6.9f / (std::max(decaySec, 0.0005f) * kSampleRate));
    m_value = attackSec > 0.0f ? 0.0f : 1.0f;
    m_attacking = attackSec > 0.0f;
  }
  float Next() {
    if (m_attacking) {
      m_value += m_attackStep;
      if (m_value >= 1.0f) {
        m_value = 1.0f;
        m_attacking = false;
      }
    } else {
      m_value *= m_decayMul;
    }
    return m_value;
  }
  bool Done() const { return !m_attacking && m_value < 0.0005f; }
  float Value() const { return m_value; }
  void Kill() { m_value = 0.0f, m_attacking = false; }

private:
  float m_value = 0.0f;
  float m_attackStep = 1.0f;
  float m_decayMul = 0.0f;
  bool m_attacking = false;
};

// Glides toward a target so parameter jumps don't click
class Smoothed {
public:
  explicit Smoothed(float timeSec = 0.05f, float initial = 0.0f)
      : m_value(initial), m_target(initial) {
    SetTime(timeSec);
  }
  void SetTime(float timeSec) {
    m_coef = 1.0f - std::exp(-1.0f / (std::max(timeSec, 1e-4f) * kSampleRate));
  }
  void SetTarget(float target) { m_target = target; }
  float Next() { return m_value += m_coef * (m_target - m_value); }
  float Value() const { return m_value; }

private:
  float m_value, m_target;
  float m_coef = 1.0f;
};

class Oscillator {
public:
  void Reset(float phase = 0.0f) { m_phase = phase; }
  float Sine(float hz) { return std::sin(kTwoPi * Advance(hz)); }
  float Triangle(float hz) {
    float p = Advance(hz);
    return 4.0f * std::fabs(p - 0.5f) - 1.0f;
  }

private:
  float Advance(float hz) {
    float p = m_phase;
    m_phase += hz / kSampleRate;
    m_phase -= std::floor(m_phase);
    return p;
  }
  float m_phase = 0.0f;
};

// Transparent below 0.8, then bends smoothly toward (never past) 1
inline float SoftClip(float x) {
  float a = std::fabs(x);
  if (a <= 0.8f)
    return x;
  return std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
}

// Constant-power pan: -1 left .. 1 right
inline void PanGains(float pan, float &left, float &right) {
  float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
  left = std::cos(angle);
  right = std::sin(angle);
}

} // namespace dsp
