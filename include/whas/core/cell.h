#pragma once
#include "whas/core/element.h"
#include <cstdint>

// Cell::flags bits
constexpr uint8_t CELL_BURNING = 1 << 0; // flammable cell on fire, fuel in lifetime
constexpr uint8_t CELL_CHARRED = 1 << 1; // scorched by fire (drawn darker)

struct Cell {
  Element element = Element::AIR;

  // Physical Properties
  float temperature = 20.0f; // Celsius
  float pressure = 0.0f;    // Pascal
  float mass = 0.0f;        // Grams
  float density = 0.0f;     // g/cm^3

  // Momentum / Inertia System
  float vx = 0.0f;      // Pixels per frame
  float vy = 0.0f;      // Pixels per frame
  float inertia = 0.0f; // Resistance to change in velocity

  // Rigid Body Integration
  int32_t bodyID = -1;     // ID of the Box2D body this cell belongs to
  int32_t triangleID = -1; // ID of the triangle in the rigid body mesh
  float u = 0.0f, v = 0.0f; // UV coordinates within the triangle/body

  // Simulation Metadata
  uint32_t lastUpdateFrame = 0; // The frame index this cell was last updated
  bool isStatic = false;        // For rigid body optimization
  uint8_t flags = 0;            // CELL_* bits

  // Element-specific properties (legacy/extended)
  float lifetime = 0.0f; // Seconds
  float hardness = 0.0f;
  float moisture = 0.0f;
};
