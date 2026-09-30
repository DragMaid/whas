#pragma once
#include <cstdint>
#include <limits>

// Deterministic RNG for lockstep play. The output depends only on the seed,
// the stream and how many values were drawn, never on threads or the platform
// (unlike std::rand and std::uniform_* distributions). Each chunk gets its own
// stream per frame, so results don't depend on which worker ran it.
class DetRng {
public:
  using result_type = uint32_t;

  explicit DetRng(uint64_t seed = 0, uint64_t stream = 0)
      : m_state(Mix(seed ^ Mix(stream + 0x9E3779B97F4A7C15ull))) {}

  static constexpr result_type min() { return 0; }
  static constexpr result_type max() {
    return std::numeric_limits<result_type>::max();
  }

  result_type operator()() {
    m_state += 0x9E3779B97F4A7C15ull;
    return static_cast<result_type>(Mix(m_state) >> 32);
  }

  // [0, n) without the platform-dependent std::uniform_int_distribution
  uint32_t Below(uint32_t n) { return n ? (*this)() % n : 0; }

  // [0, 1)
  float Unit() { return static_cast<float>((*this)() >> 8) * (1.0f / 16777216.0f); }

  // For snapshots
  uint64_t State() const { return m_state; }
  static DetRng FromState(uint64_t state) {
    DetRng r;
    r.m_state = state;
    return r;
  }

  // SplitMix64 finalizer
  static uint64_t Mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

private:
  uint64_t m_state;
};
