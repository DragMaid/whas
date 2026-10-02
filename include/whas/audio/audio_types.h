#pragma once
#include "whas/core/element.h"
#include <cstddef>
#include <cstdint>

// One synthesized voice family. Adding a sound: a value here, a Synth
// subclass, a line in CreateSynth (audio_manager.cpp) and the elements that
// use it in element_sound_map.h.
enum class SoundProfile : uint8_t {
  Sand,
  Wind,
  Fire,
  Earth,
  Water,
  Light,
  Spell,  // a spell being cast
  Break,  // solid material shattering
  Flight, // a caster flying on wind underfoot

  COUNT,
  None = 0xFF,
};

constexpr size_t SOUND_PROFILE_COUNT = static_cast<size_t>(SoundProfile::COUNT);

inline const char *SoundProfileName(SoundProfile profile) {
  switch (profile) {
  case SoundProfile::Sand:
    return "Sand";
  case SoundProfile::Wind:
    return "Wind";
  case SoundProfile::Fire:
    return "Fire";
  case SoundProfile::Earth:
    return "Earth";
  case SoundProfile::Water:
    return "Water";
  case SoundProfile::Light:
    return "Light";
  case SoundProfile::Spell:
    return "Spells";
  case SoundProfile::Break:
    return "Breaking";
  case SoundProfile::Flight:
    return "Flight";
  default:
    return "None";
  }
}

// What happened, for profiles that sound different per cause
enum class SoundKind : uint8_t {
  Settle, // came to rest (the default)
  Impact, // a spell's element struck something
  Break,  // material shattered
  Cast,   // a spell left the caster
  Fizzle, // fire met water
  Launch, // wind underfoot took off
};

// A one-shot sound (a grain of sand landing, a stone settling)
struct AudioEvent {
  SoundProfile profile = SoundProfile::None;
  float gain = 1.0f;  // 0..1
  float pan = 0.0f;   // -1 left .. 1 right
  float pitch = 1.0f; // frequency multiplier
  SoundKind kind = SoundKind::Settle;
  Element element = Element::AIR; // the material, for its colour
};

// The continuous part of a profile (wind blowing, fire burning), refreshed
// every frame
struct ProfileState {
  float level = 0.0f;   // 0..1 how loud the bed is
  float density = 0.0f; // 0..1 how much is going on; brightens and thickens
  float pan = 0.0f;     // -1 left .. 1 right
};
