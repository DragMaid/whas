#pragma once
#include <cstdint>

enum class Element : uint8_t {
  AIR = 0,
  WATER = 1,
  EARTH = 2,
  FIRE = 3,
  STEAM = 4,
  CLOUD = 5,
  ICE = 6,
  SAND = 7,
  ROCK = 8,
  WOOD = 9,
  GRASS = 10,
  SMOKE = 11,
  // Only ever a spell projectile: it bursts into a flash instead of landing
  LIGHT = 12,

  COUNT
};

inline const char *ElementName(Element element) {
  switch (element) {
  case Element::AIR:
    return "AIR";
  case Element::WATER:
    return "WATER";
  case Element::EARTH:
    return "EARTH";
  case Element::FIRE:
    return "FIRE";
  case Element::STEAM:
    return "STEAM";
  case Element::CLOUD:
    return "CLOUD";
  case Element::ICE:
    return "ICE";
  case Element::SAND:
    return "SAND";
  case Element::ROCK:
    return "ROCK";
  case Element::WOOD:
    return "WOOD";
  case Element::GRASS:
    return "GRASS";
  case Element::SMOKE:
    return "SMOKE";
  case Element::LIGHT:
    return "LIGHT";
  default:
    return "UNKNOWN";
  }
}
