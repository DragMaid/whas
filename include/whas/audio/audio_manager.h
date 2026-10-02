#pragma once
#include "whas/audio/audio_types.h"
#include "whas/audio/spsc_queue.h"
#include "whas/audio/synth.h"
#include <array>
#include <atomic>
#include <memory>
#include <raylib.h>
#include <vector>

// Recorded sounds for the interface; the files are listed in audio_manager.cpp
enum class UiSound : uint8_t { Button, SpellPlan, Draw, COUNT };

// Volumes the player can change (main thread; pushed to the mixer in Update)
struct AudioSettings {
  float master = 0.8f;
  float ambient = 0.8f; // every world sound
  float ui = 0.7f;
  std::array<float, SOUND_PROFILE_COUNT> profile = [] {
    std::array<float, SOUND_PROFILE_COUNT> v{};
    v.fill(1.0f);
    return v;
  }();
  bool muted = false;
};

// Owns the audio stream and the procedural synths. The main thread posts
// events and states; raylib's audio thread mixes them through a limiter so
// any number of particles can't clip.
class AudioManager {
public:
  static constexpr int kSampleRate = 48000;

  // Builds the synths; doesn't touch the audio device (tests use Mix)
  AudioManager();
  ~AudioManager();
  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;

  // Opens the stream and loads the button sound. Call after
  // InitAudioDevice(); false (and silence) without a device.
  bool Init();
  // Stops the stream and frees the sounds; call before CloseAudioDevice()
  void Shutdown();

  // The one live manager, for UI widgets (nullptr before construction)
  static AudioManager *Instance() { return s_instance; }
  // Post to the live manager if there is one (headless runs have none)
  static void Emit(const AudioEvent &event) {
    if (s_instance)
      s_instance->Post(event);
  }
  // Wind underfoot taking someone off at `x` cells
  static void EmitFlightLaunch(float x);

  // Main thread
  void Post(const AudioEvent &event);
  void SetProfileState(SoundProfile profile, const ProfileState &state);
  void PlayUi(UiSound sound);
  void PlayUiClick() { PlayUi(UiSound::Button); }
  // Once a frame: applies settings changes. A profile state nobody has set
  // for a few frames (its source went away) falls silent.
  void Update();
  AudioSettings &Settings() { return m_settings; }
  int VoicesInUse(SoundProfile profile) const;
  float Level(SoundProfile profile) const;
  // Bus gain reduction from the last block (1 = untouched)
  float LimiterGain() const { return m_limiterReport.load(std::memory_order_relaxed); }

  // Audio thread (or a test): mix `frames` interleaved stereo float frames
  void Mix(float *out, unsigned frames);

private:
  static void Callback(void *buffer, unsigned frames);
  static constexpr int kBlock = 256;

  struct AtomicState {
    std::atomic<float> level{0.0f};
    std::atomic<float> density{0.0f};
    std::atomic<float> pan{0.0f};
  };

  inline static AudioManager *s_instance = nullptr;

  std::array<std::unique_ptr<Synth>, SOUND_PROFILE_COUNT> m_synths;
  std::array<AtomicState, SOUND_PROFILE_COUNT> m_states;
  std::array<std::atomic<float>, SOUND_PROFILE_COUNT> m_profileGain;
  std::atomic<float> m_masterGain{0.0f};
  SpscQueue<AudioEvent, 1024> m_events;

  // Audio thread only
  std::array<float, kBlock> m_scratchL{}, m_scratchR{};
  std::array<float, kBlock> m_busL{}, m_busR{};
  float m_limiterEnv = 0.0f;
  float m_masterSmoothed = 0.0f;
  std::array<float, SOUND_PROFILE_COUNT> m_profileSmoothed{};
  std::atomic<float> m_limiterReport{1.0f};

  AudioSettings m_settings;
  AudioStream m_stream{};
  bool m_streamReady = false;
  // Per UiSound: the sound and aliases of it, so quick repeats overlap
  struct UiVoices {
    std::vector<Sound> sounds;
    size_t next = 0;
  };
  std::array<UiVoices, static_cast<size_t>(UiSound::COUNT)> m_ui;
  std::array<int, SOUND_PROFILE_COUNT> m_stateAge{};
};
