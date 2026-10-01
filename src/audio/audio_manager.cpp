#include "whas/audio/audio_manager.h"
#include "whas/constants.h"
#include <algorithm>
#include <iterator>
#include <cmath>

namespace {

// Per UiSound
constexpr const char *kUiSoundFiles[] = {
    "assets/audios/button.mp3",
    "assets/audios/spell-plan.mp3",
    "assets/audios/draw.mp3",
};
static_assert(std::size(kUiSoundFiles) == static_cast<size_t>(UiSound::COUNT));
constexpr int kUiVoices = 4;
// Frames a profile state lives without being set again
constexpr int kStateFrames = 3;
// Peak limiter on the bus: instant attack, ~150 ms release, -1 dBFS ceiling
constexpr float kLimiterCeiling = 0.89f;
const float kLimiterRelease = std::exp(-1.0f / (0.15f * 48000.0f));

// The synth for each profile; a new profile adds its line here
std::unique_ptr<Synth> CreateSynth(SoundProfile profile) {
  switch (profile) {
  case SoundProfile::Sand:
    return MakeSandSynth();
  case SoundProfile::Wind:
    return MakeWindSynth();
  case SoundProfile::Fire:
    return MakeFireSynth();
  case SoundProfile::Earth:
    return MakeEarthSynth();
  case SoundProfile::Water:
    return MakeWaterSynth();
  case SoundProfile::Light:
    return MakeLightSynth();
  case SoundProfile::Spell:
    return MakeSpellSynth();
  case SoundProfile::Break:
    return MakeBreakSynth();
  case SoundProfile::Flight:
    return MakeFlightSynth();
  default:
    return nullptr;
  }
}

} // namespace

AudioManager::AudioManager() {
  for (size_t i = 0; i < SOUND_PROFILE_COUNT; ++i) {
    m_synths[i] = CreateSynth(static_cast<SoundProfile>(i));
    m_profileGain[i].store(1.0f);
  }
  s_instance = this;
  Update();
}

AudioManager::~AudioManager() {
  Shutdown();
  if (s_instance == this)
    s_instance = nullptr;
}

bool AudioManager::Init() {
  if (!IsAudioDeviceReady()) {
    TraceLog(LOG_WARNING, "AUDIO: no device, the game will be silent");
    return false;
  }
  // Small blocks keep impacts tight to what's on screen
  SetAudioStreamBufferSizeDefault(1024);
  m_stream = LoadAudioStream(kSampleRate, 32, 2);
  if (!IsAudioStreamValid(m_stream))
    return false;
  SetAudioStreamCallback(m_stream, &AudioManager::Callback);
  PlayAudioStream(m_stream);
  m_streamReady = true;

  for (size_t i = 0; i < m_ui.size(); ++i) {
    const char *file = kUiSoundFiles[i];
    if (!FileExists(file)) {
      TraceLog(LOG_WARNING, "AUDIO: %s missing, it won't play", file);
      continue;
    }
    Sound source = LoadSound(file);
    if (!IsSoundValid(source))
      continue;
    m_ui[i].sounds.push_back(source);
    for (int v = 1; v < kUiVoices; ++v)
      m_ui[i].sounds.push_back(LoadSoundAlias(source));
  }
  Update();
  return true;
}

void AudioManager::Shutdown() {
  if (m_streamReady) {
    StopAudioStream(m_stream);
    UnloadAudioStream(m_stream);
    m_streamReady = false;
  }
  for (UiVoices &voices : m_ui) {
    for (size_t i = voices.sounds.size(); i-- > 1;)
      UnloadSoundAlias(voices.sounds[i]);
    if (!voices.sounds.empty())
      UnloadSound(voices.sounds[0]);
    voices.sounds.clear();
  }
}

void AudioManager::Post(const AudioEvent &event) {
  if (event.profile >= SoundProfile::COUNT || event.gain <= 0.0f)
    return;
  m_events.Push(event); // full queue: dropped, which is fine for sound
}

void AudioManager::EmitFlightLaunch(float x) {
  float pan = std::clamp(x / GRID_W * 2.0f - 1.0f, -1.0f, 1.0f) * 0.8f;
  Emit({SoundProfile::Flight, 0.9f, pan, 1.0f, SoundKind::Launch});
}

void AudioManager::SetProfileState(SoundProfile profile,
                                   const ProfileState &state) {
  if (profile >= SoundProfile::COUNT)
    return;
  m_stateAge[static_cast<size_t>(profile)] = 0;
  AtomicState &s = m_states[static_cast<size_t>(profile)];
  s.level.store(std::clamp(state.level, 0.0f, 1.0f), std::memory_order_relaxed);
  s.density.store(std::clamp(state.density, 0.0f, 1.0f),
                  std::memory_order_relaxed);
  s.pan.store(std::clamp(state.pan, -1.0f, 1.0f), std::memory_order_relaxed);
}

void AudioManager::PlayUi(UiSound sound) {
  UiVoices &voices = m_ui[static_cast<size_t>(sound)];
  if (voices.sounds.empty() || m_settings.muted)
    return;
  Sound &voice = voices.sounds[voices.next];
  voices.next = (voices.next + 1) % voices.sounds.size();
  SetSoundVolume(voice, m_settings.master * m_settings.ui);
  PlaySound(voice);
}

void AudioManager::Update() {
  const AudioSettings &s = m_settings;
  m_masterGain.store(s.muted ? 0.0f : s.master * s.ambient,
                     std::memory_order_relaxed);
  for (size_t i = 0; i < SOUND_PROFILE_COUNT; ++i) {
    m_profileGain[i].store(s.profile[i], std::memory_order_relaxed);
    if (++m_stateAge[i] > kStateFrames)
      m_states[i].level.store(0.0f, std::memory_order_relaxed);
  }
}

int AudioManager::VoicesInUse(SoundProfile profile) const {
  const auto &synth = m_synths[static_cast<size_t>(profile)];
  return synth ? synth->VoicesInUse() : 0;
}

float AudioManager::Level(SoundProfile profile) const {
  return m_states[static_cast<size_t>(profile)].level.load(
      std::memory_order_relaxed);
}

void AudioManager::Callback(void *buffer, unsigned frames) {
  if (s_instance)
    s_instance->Mix(static_cast<float *>(buffer), frames);
}

void AudioManager::Mix(float *out, unsigned frames) {
  AudioEvent event;
  while (m_events.Pop(event))
    if (auto &synth = m_synths[static_cast<size_t>(event.profile)])
      synth->Trigger(event);

  for (size_t i = 0; i < SOUND_PROFILE_COUNT; ++i) {
    if (!m_synths[i])
      continue;
    const AtomicState &s = m_states[i];
    m_synths[i]->SetState({s.level.load(std::memory_order_relaxed),
                           s.density.load(std::memory_order_relaxed),
                           s.pan.load(std::memory_order_relaxed)});
  }

  float minGain = 1.0f;
  unsigned done = 0;
  while (done < frames) {
    int n = static_cast<int>(std::min<unsigned>(kBlock, frames - done));
    std::fill_n(m_busL.begin(), n, 0.0f);
    std::fill_n(m_busR.begin(), n, 0.0f);

    // Each profile renders on its own so its volume ramps without zipper
    for (size_t i = 0; i < SOUND_PROFILE_COUNT; ++i) {
      if (!m_synths[i])
        continue;
      std::fill_n(m_scratchL.begin(), n, 0.0f);
      std::fill_n(m_scratchR.begin(), n, 0.0f);
      m_synths[i]->Render(m_scratchL.data(), m_scratchR.data(), n);
      float from = m_profileSmoothed[i];
      float to = m_profileGain[i].load(std::memory_order_relaxed);
      float step = (to - from) / n;
      for (int k = 0; k < n; ++k) {
        float g = from + step * (k + 1);
        m_busL[k] += m_scratchL[k] * g;
        m_busR[k] += m_scratchR[k] * g;
      }
      m_profileSmoothed[i] = to;
    }

    float masterTo = m_masterGain.load(std::memory_order_relaxed);
    float masterStep = (masterTo - m_masterSmoothed) / n;
    for (int k = 0; k < n; ++k) {
      m_masterSmoothed += masterStep;
      float l = m_busL[k] * m_masterSmoothed;
      float r = m_busR[k] * m_masterSmoothed;
      if (!std::isfinite(l) || !std::isfinite(r))
        l = r = 0.0f;

      // Peak limiter, then a soft clip as the last guard
      float peak = std::max(std::fabs(l), std::fabs(r));
      m_limiterEnv = peak > m_limiterEnv
                         ? peak
                         : peak + kLimiterRelease * (m_limiterEnv - peak);
      float gain =
          m_limiterEnv > kLimiterCeiling ? kLimiterCeiling / m_limiterEnv : 1.0f;
      minGain = std::min(minGain, gain);
      out[2 * (done + k)] = dsp::SoftClip(l * gain);
      out[2 * (done + k) + 1] = dsp::SoftClip(r * gain);
    }
    done += n;
  }
  m_limiterReport.store(minGain, std::memory_order_relaxed);
}
