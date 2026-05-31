#include "whas/element/base/properties.h"

// Air
constexpr ElementProperties MakeAir() {
  return {false, false, true, 1.2f, 20.0f, 0.0f, false, 0.0f};
}

// Water
constexpr ElementProperties MakeWater() {
  return {true, false, false, 1000.0f, 15.0f, 1.0f, true, 0.0f};
}

// Earth
constexpr ElementProperties MakeEarth() {
  return {false, true, false, 2000.0f, 20.0f, 2.0f, false, 80.0f};
}

// Fire
constexpr ElementProperties MakeFire() {
  return {false, false, true, 0.5f, 800.0f, 0.0f, false, 0.0f};
}

// Steam
constexpr ElementProperties MakeSteam() {
  return {true, false, true, 0.6f, 105.0f, 0.1f, false, 0.0f};
}

// Cloud
constexpr ElementProperties MakeCloud() {
  return {true, false, true, 0.3f, 5.0f, 0.0f, false, 0.0f};
}

// Ice
constexpr ElementProperties MakeIce() {
  return {false, true, false, 917.0f, -5.0f, 0.9f, false, 100.0f};
}

// NOTE: the registry need same ordering as the Enum
PropertiesArray ElementRegistry::s_properties = {
    MakeAir(),   MakeWater(), MakeEarth(), MakeFire(),
    MakeSteam(), MakeCloud(), MakeIce()};

const ElementProperties &ElementRegistry::GetProperties(Element element) {
  return s_properties[static_cast<size_t>(element)];
}
