#include "whas/audio/audio_observer.h"
#include "whas/audio/audio_manager.h"
#include "whas/audio/element_sound_map.h"
#include "whas/constants.h"
#include "whas/engine/simulation.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr size_t kCells = static_cast<size_t>(GRID_W) * GRID_H;
// More of the grid than this changing at once is a reset or a snapshot
// load, not something to hear
constexpr size_t kResyncCells = kCells / 4;
constexpr size_t kBodyHitsPerTick = 3;

size_t Index(SoundProfile p) { return static_cast<size_t>(p); }

float PanOf(float x) {
  return std::clamp((x / GRID_W) * 2.0f - 1.0f, -1.0f, 1.0f) * 0.8f;
}

// 0..1, ~63% at `scale`, never quite 1: many cells stop adding loudness
float Saturate(float amount, float scale) {
  return 1.0f - std::exp(-amount / std::max(scale, 1e-3f));
}

// Which profile voices a particle noise
SoundProfile ProfileOf(const ParticleNoise &n) {
  switch (n.kind) {
  case ParticleNoise::Cast:
    return SoundProfile::Spell;
  case ParticleNoise::Break:
    return SoundProfile::Break;
  case ParticleNoise::Fizzle:
    return SoundProfile::Fire;
  case ParticleNoise::BodyHit:
    return SoundProfile::None; // heard on their own, see HearNoises
  case ParticleNoise::Impact:
    switch (n.element) {
    case Element::FIRE:
      return SoundProfile::Fire;
    case Element::WATER:
      return SoundProfile::Water;
    case Element::ICE:
      return SoundProfile::Break; // shards shatter where they land
    case Element::EARTH:
    case Element::SAND:
      return SoundProfile::Earth;
    default:
      return SoundProfile::None; // light bursts are heard on their own
    }
  }
  return SoundProfile::None;
}

SoundKind KindOf(const ParticleNoise &n) {
  switch (n.kind) {
  case ParticleNoise::Cast:
    return SoundKind::Cast;
  case ParticleNoise::Break:
    return SoundKind::Break;
  case ParticleNoise::Fizzle:
    return SoundKind::Fizzle;
  default:
    return n.element == Element::ICE ? SoundKind::Break : SoundKind::Impact;
  }
}

// Noises of one sound this tick, batched into a few events
struct NoiseBatch {
  SoundProfile profile;
  SoundKind kind;
  Element element;
  int count = 0;
  int samples = 0;
  std::array<float, 4> sampleX{};
};

// Per kind of noise: events per tick at most, and how many noises in one
// tick make a full-gain event
struct NoiseTuning {
  int maxEvents;
  float full;
};

NoiseTuning TuningOf(SoundKind kind) {
  switch (kind) {
  case SoundKind::Cast:
    return {3, 2.0f};
  case SoundKind::Break:
    return {3, 12.0f};
  case SoundKind::Fizzle:
    return {2, 20.0f};
  default: // Impact
    return {3, 10.0f};
  }
}

} // namespace

AudioObserver::AudioObserver() : m_prev(kCells, 0), m_moving(kCells, 0) {
  auto tune = [&](SoundProfile p, Tuning t) { m_tuning[Index(p)] = t; };
  // events/tick, full-gain settle weight, bed scale, density scale, gap (s)
  tune(SoundProfile::Sand, {6, 120.0f, 60.0f, 600.0f, 0.0f});
  tune(SoundProfile::Earth, {3, 20.0f, 1.0f, 300.0f, 0.04f});
  tune(SoundProfile::Water, {1, 400.0f, 300.0f, 3000.0f, 0.12f});
  tune(SoundProfile::Fire, {0, 1.0f, 80.0f, 800.0f, 0.0f});
  tune(SoundProfile::Wind, {0, 1.0f, 250.0f, 1500.0f, 0.0f});
  tune(SoundProfile::Light, {4, 1.0f, 1.0f, 1.0f, 0.0f});
}

void AudioObserver::Update(Simulation &sim, AudioManager &audio, float dt) {
  m_tally = {};
  uint32_t frame = sim.GetFrame();
  if (frame < m_lastFrame) // Restart: a new world, nothing to compare
    m_primed = false;
  m_ticked = m_primed && frame != m_lastFrame;
  m_lastFrame = frame;

  Scan(sim);
  ScanSpells(sim, audio);
  HearNoises(sim, audio);
  Emit(audio, dt);
}

void AudioObserver::Sample(Tally &t, float x) {
  // Reservoir sampling: every settle equally likely to place an impact
  ++t.seen;
  if (t.samples < kSamples) {
    t.sampleX[t.samples++] = x;
  } else {
    uint32_t j = m_rng.Next() % static_cast<uint32_t>(t.seen);
    if (j < kSamples)
      t.sampleX[j] = x;
  }
}

void AudioObserver::Scan(const Simulation &sim) {
  bool diff = m_ticked;
  size_t changed = 0;
  Tally &fire = m_tally[Index(SoundProfile::Fire)];

  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) {
      size_t i = static_cast<size_t>(y) * GRID_W + x;
      const Cell &cell = sim.GetCell(x, y);
      auto element = static_cast<uint8_t>(cell.element);
      float fx = static_cast<float>(x);

      // Fire is heard for as long as it burns, moving or not
      if (cell.element == Element::FIRE || (cell.flags & CELL_BURNING)) {
        fire.moveWeight += 1.0f;
        fire.sumX += fx;
        fire.weight += 1.0f;
      }
      if (!diff)
        continue;

      bool movedNow = element != m_prev[i];
      const ElementSound &sound = SoundOf(cell.element);
      if (movedNow) {
        ++changed;
        if (sound.onMove) {
          Tally &t = m_tally[Index(sound.profile)];
          t.moveWeight += sound.weight;
          t.sumX += fx * sound.weight;
          t.weight += sound.weight;
        }
      } else if (m_moving[i] && sound.onSettle) {
        Tally &t = m_tally[Index(sound.profile)];
        t.settleWeight += sound.weight;
        t.sumX += fx * sound.weight;
        t.weight += sound.weight;
        Sample(t, fx);
      }
      m_moving[i] = movedNow;
      m_prev[i] = element;
    }
  }

  if (!m_ticked) {
    // First look (or nothing moved): remember the world, hear only fire
    if (!m_primed) {
      for (int y = 0; y < GRID_H; ++y)
        for (int x = 0; x < GRID_W; ++x)
          m_prev[static_cast<size_t>(y) * GRID_W + x] =
              static_cast<uint8_t>(sim.GetCell(x, y).element);
      std::fill(m_moving.begin(), m_moving.end(), 0);
      m_primed = true;
    }
    return;
  }
  if (changed > kResyncCells) {
    // Keep the fire, drop the avalanche of fake moves
    Tally keep = fire;
    m_tally = {};
    m_tally[Index(SoundProfile::Fire)] = keep;
    std::fill(m_moving.begin(), m_moving.end(), 0);
  }
}

void AudioObserver::ScanSpells(const Simulation &sim, AudioManager &audio) {
  // Wind: field spells blow while they last; a new one swells in
  int fields = 0;
  float force = 0.0f, sumX = 0.0f;
  for (const SpellEffect &effect : sim.GetActiveSpellEffects()) {
    if (effect.stats.kind != SpellKind::Field)
      continue;
    ++fields;
    force += 0.6f;
    sumX += effect.origin.x;
  }
  if (fields > m_lastFieldCount)
    audio.Post({SoundProfile::Wind, 0.8f, PanOf(sumX / fields), 1.0f});
  m_lastFieldCount = fields;
  m_windForce = std::min(1.0f, force);
  if (fields > 0) {
    Tally &wind = m_tally[Index(SoundProfile::Wind)];
    wind.sumX += sumX * 4.0f;
    wind.weight += fields * 4.0f;
  }

  // Light: every burst since the last look, up to a few per tick
  const ParticleSystem &particles = sim.GetParticleSystem();
  uint32_t bursts = particles.BurstCount();
  uint32_t fresh = std::min<uint32_t>(
      bursts - m_lastBurst,
      static_cast<uint32_t>(m_tuning[Index(SoundProfile::Light)].maxEventsPerTick));
  if (m_primed && bursts != m_lastBurst) {
    float pan = PanOf(particles.LastBurstPos().x);
    for (uint32_t i = 0; i < fresh; ++i)
      audio.Post({SoundProfile::Light, 0.7f, pan + 0.15f * m_rng.Bipolar(),
                  m_rng.Range(0.9f, 1.15f)});
  }
  m_lastBurst = bursts;
}

void AudioObserver::HearNoises(Simulation &sim, AudioManager &audio) {
  std::vector<ParticleNoise> noises = sim.GetParticleSystem().TakeNoises();
  if (!m_primed || !m_ticked)
    return; // a fresh world's leftovers, or nothing new

  // Rigid bodies striking: the hardest few, each as loud as it hit
  std::vector<const ParticleNoise *> hits;
  for (const ParticleNoise &n : noises)
    if (n.kind == ParticleNoise::BodyHit)
      hits.push_back(&n);
  std::sort(hits.begin(), hits.end(),
            [](auto *a, auto *b) { return a->strength > b->strength; });
  hits.resize(std::min<size_t>(hits.size(), kBodyHitsPerTick));
  for (const ParticleNoise *n : hits) {
    AudioEvent ev;
    ev.profile = SoundProfile::Earth;
    ev.element = n->element;
    ev.gain = 0.35f + 0.65f * n->strength;
    ev.pan = PanOf(n->pos.x);
    // Bigger bodies are deeper; ice rings a little higher
    ev.pitch = (n->element == Element::ICE ? 1.4f : 1.2f) - 0.6f * n->heft;
    audio.Post(ev);
  }

  std::vector<NoiseBatch> batches;
  for (const ParticleNoise &n : noises) {
    SoundProfile profile = ProfileOf(n);
    if (profile == SoundProfile::None)
      continue;
    SoundKind kind = KindOf(n);
    auto it = std::find_if(batches.begin(), batches.end(), [&](const auto &b) {
      return b.profile == profile && b.kind == kind && b.element == n.element;
    });
    if (it == batches.end()) {
      batches.push_back({profile, kind, n.element});
      it = batches.end() - 1;
    }
    // Reservoir: every noise equally likely to place an event
    ++it->count;
    if (it->samples < static_cast<int>(it->sampleX.size()))
      it->sampleX[it->samples++] = n.pos.x;
    else if (uint32_t j = m_rng.Next() % it->count; j < it->sampleX.size())
      it->sampleX[j] = n.pos.x;
  }

  for (const NoiseBatch &b : batches) {
    NoiseTuning tune = TuningOf(b.kind);
    int events = std::min({tune.maxEvents, b.samples, b.count});
    float total = std::log1p(static_cast<float>(b.count)) / std::log1p(tune.full);
    float gain = std::min(1.0f, 0.55f + 0.45f * total) /
                 std::sqrt(static_cast<float>(events));
    for (int e = 0; e < events; ++e) {
      AudioEvent ev;
      ev.profile = b.profile;
      ev.kind = b.kind;
      ev.element = b.element;
      ev.gain = gain;
      ev.pan = PanOf(b.sampleX[e]);
      ev.pitch = m_rng.Range(0.92f, 1.08f);
      audio.Post(ev);
    }
  }
}

void AudioObserver::Emit(AudioManager &audio, float dt) {
  for (size_t p = 0; p < SOUND_PROFILE_COUNT; ++p) {
    auto profile = static_cast<SoundProfile>(p);
    const Tally &t = m_tally[p];
    const Tuning &tune = m_tuning[p];
    float pan = t.weight > 0.0f ? PanOf(t.sumX / t.weight) : 0.0f;

    // The continuous bed (fire, flowing sand and water, wind)
    ProfileState state;
    state.level = Saturate(t.moveWeight, tune.bedScale);
    state.density = Saturate(t.moveWeight + t.settleWeight, tune.densityScale);
    state.pan = pan;
    if (profile == SoundProfile::Wind)
      state.level = std::min(1.0f, m_windForce + 0.6f * state.level);
    if (profile == SoundProfile::Earth)
      state.level = 0.0f; // earth only thumps
    audio.SetProfileState(profile, state);

    // Impacts, batched: a few events stand in for every settle this tick
    m_cooldown[p] = std::max(0.0f, m_cooldown[p] - dt);
    if (tune.maxEventsPerTick <= 0 || t.settleWeight <= 0.0f ||
        m_cooldown[p] > 0.0f)
      continue;
    int events = std::clamp(static_cast<int>(std::ceil(t.settleWeight / 3.0f)),
                            1, std::min(tune.maxEventsPerTick, t.samples));
    // Total loudness grows with the log of the count; split so the events
    // together (summed in power) carry it
    float total = std::log1p(t.settleWeight) / std::log1p(tune.settleFull);
    float gain = std::min(1.0f, total) / std::sqrt(static_cast<float>(events));
    // Heavy settles sound lower (earth), dense ones slightly brighter (sand)
    float heft = Saturate(t.settleWeight, tune.settleFull);
    for (int e = 0; e < events; ++e) {
      AudioEvent ev;
      ev.profile = profile;
      ev.gain = gain;
      ev.pan = PanOf(t.sampleX[e]);
      ev.pitch = profile == SoundProfile::Earth ? 1.25f - 0.6f * heft
                                                : 1.0f + 0.15f * heft;
      audio.Post(ev);
    }
    m_cooldown[p] = tune.minInterval;
  }
}
