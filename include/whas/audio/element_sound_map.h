#pragma once
#include "whas/audio/audio_types.h"
#include "whas/core/element.h"
#include <array>

// How a cell of each element sounds. The observer counts a cell that changed
// this frame as a move and one that just stopped changing as a settle.
struct ElementSound {
  SoundProfile profile = SoundProfile::None;
  float weight = 0.0f; // how much one cell counts (a stone outweighs a grain)
  bool onMove = false;   // moving cells feed the profile's continuous bed
  bool onSettle = false; // settling cells trigger one-shot impacts
};

using ElementSoundMap =
    std::array<ElementSound, static_cast<size_t>(Element::COUNT)>;

// A new element is one line here; one left out stays silent
constexpr ElementSoundMap kElementSounds = [] {
  ElementSoundMap map{};
  auto set = [&](Element e, ElementSound sound) {
    map[static_cast<size_t>(e)] = sound;
  };
  set(Element::AIR, {});
  set(Element::WATER, {SoundProfile::Water, 1.0f, true, true});
  set(Element::EARTH, {SoundProfile::Earth, 1.0f, false, true});
  set(Element::FIRE, {SoundProfile::Fire, 1.0f, true, false});
  set(Element::STEAM, {SoundProfile::Wind, 0.6f, true, false});
  set(Element::CLOUD, {SoundProfile::Wind, 0.3f, true, false});
  set(Element::ICE, {SoundProfile::Earth, 0.7f, false, true});
  set(Element::SAND, {SoundProfile::Sand, 1.0f, true, true});
  set(Element::ROCK, {SoundProfile::Earth, 1.4f, false, true});
  // Wood and grass only sound when burning (see AudioObserver)
  set(Element::WOOD, {});
  set(Element::GRASS, {});
  set(Element::SMOKE, {SoundProfile::Wind, 0.4f, true, false});
  // Never a cell: bursts are read from the particle system
  set(Element::LIGHT, {});
  return map;
}();

inline const ElementSound &SoundOf(Element element) {
  return kElementSounds[static_cast<size_t>(element)];
}
